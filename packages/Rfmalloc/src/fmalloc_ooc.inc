//==============================================================================
// Out-of-core matrix products for fmalloc-backed matrices larger than RAM.
//
// Y = A %*% X where A is a large column-major fmalloc double matrix (m x n)
// living in the backing file, and X is a dense double matrix (n x k). A is
// consumed one contiguous column tile at a time: each tile is multiplied into
// the accumulator with BLAS dgemm, then its pages are released with
// MADV_DONTNEED so the resident set stays bounded by the tile budget rather
// than the matrix size. MADV_SEQUENTIAL hints the pager to read ahead.
//==============================================================================

// <sys/mman.h> (for madvise) is unavailable in the Rtools toolchain used for
// the R package compile on Windows; the OOC path degrades to a no-op there.
#if !defined(_WIN32)
#include <sys/mman.h>
#endif
#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__)
#include <sys/sysctl.h>
#endif

// madvise()/MADV_* are POSIX but absent on some platforms - the OOC path still
// works there, just without per-tile eviction.
#if defined(MADV_DONTNEED) && defined(MADV_SEQUENTIAL) && !defined(_WIN32)
#  define RFM_HAVE_MADVISE 1
#else
#  define RFM_HAVE_MADVISE 0
#endif

// macOS/BSD mach headers (pulled in by <sys/sysctl.h>/<sys/mman.h>) define
// FALSE/TRUE as macros, which shadow R's Rboolean enumerators. Since R 4.5
// Rboolean is a fixed-underlying-type enum that an int does not implicitly
// convert to, that shadowing breaks Rboolean-typed R API calls (e.g.
// R_useDynamicSymbols) in translation units included after this one. Drop the
// macros so the enumerators are visible again.
#ifdef FALSE
#  undef FALSE
#endif
#ifdef TRUE
#  undef TRUE
#endif

// Portable page-advice request, mapped to madvise where available.
enum ooc_advice_t { OOC_SEQUENTIAL = 0, OOC_DONTNEED = 1, OOC_WILLNEED = 2 };

// Total physical RAM in bytes, or NA_REAL if it cannot be determined.
// Uses POSIX sysconf where available (Linux and most Unix), falling back to
// the BSD/macOS sysctl HW memory query.
extern "C" SEXP rfm_phys_ram_bytes_impl(void)
{
    double bytes = NA_REAL;
#if defined(_SC_PHYS_PAGES) && defined(_SC_PAGESIZE)
    long pages = sysconf(_SC_PHYS_PAGES);
    long psz = sysconf(_SC_PAGESIZE);
    if (pages > 0 && psz > 0) {
        bytes = (double)pages * (double)psz;
    }
#endif
#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__)
    if (!R_FINITE(bytes)) {
#if defined(HW_MEMSIZE)
        int mib[2] = {CTL_HW, HW_MEMSIZE};
#elif defined(HW_PHYSMEM64)
        int mib[2] = {CTL_HW, HW_PHYSMEM64};
#else
        int mib[2] = {CTL_HW, HW_PHYSMEM};
#endif
        uint64_t mem = 0;
        size_t len = sizeof(mem);
        if (sysctl(mib, 2, &mem, &len, nullptr, 0) == 0 && mem > 0) {
            bytes = (double)mem;
        }
    }
#endif
    return Rf_ScalarReal(bytes);
}

static inline uintptr_t ooc_page_size(void)
{
#if defined(_SC_PAGESIZE)
    long ps = sysconf(_SC_PAGESIZE);
    return ps > 0 ? (uintptr_t)ps : 4096;
#else
    return 4096;
#endif
}

// Advise the kernel about a payload range (best-effort; a no-op on platforms
// without madvise). Failures are ignored - they are expected for non-mmap or
// unaligned edge cases.
static void ooc_advise(void *addr, size_t bytes, ooc_advice_t advice)
{
    if (!addr || bytes == 0) {
        return;
    }
#if RFM_HAVE_MADVISE
    int m = advice == OOC_SEQUENTIAL ? MADV_SEQUENTIAL
          : advice == OOC_WILLNEED  ? MADV_WILLNEED
                                    : MADV_DONTNEED;
    uintptr_t ps = ooc_page_size();
    uintptr_t start = (uintptr_t)addr;
    uintptr_t end = start + bytes;
    uintptr_t astart = (start + ps - 1) & ~(ps - 1); // round up
    uintptr_t aend = end & ~(ps - 1);                // round down
    if (aend > astart) {
        madvise((void *)astart, (size_t)(aend - astart), m);
    }
#else
    (void)advice;
#endif
}

// Flush a runtime's backing store to disk. Writes to the MAP_SHARED mapping
// (including in-place mutations) are otherwise only written back by the kernel
// asynchronously, so a crash before writeback loses unsynced data; msync forces
// the durability barrier. wait = TRUE uses MS_SYNC (blocks until written) plus
// fsync for file metadata; FALSE schedules an asynchronous MS_ASYNC flush.
// Returns the number of bytes flushed (0 where sync is unavailable, e.g.
// Windows/Rtools, which relies on OS writeback).
extern "C" SEXP rfm_sync_impl(SEXP runtime_xptr, SEXP wait_sexp)
{
    fm_runtime *runtime = runtime_from_xptr(runtime_xptr);
    if (!runtime || !runtime->info) {
        Rf_error("fmalloc runtime is closed");
    }
    int wait = Rf_asLogical(wait_sexp) == TRUE;
    struct fm_info *info = runtime->info;
    double flushed = 0.0;
#if !defined(_WIN32)
    if (info->mem && info->len > 0) {
        if (msync(info->mem, info->len, wait ? MS_SYNC : MS_ASYNC) == 0) {
            flushed = (double)info->len;
        }
        if (wait && info->fd >= 0) {
            fsync(info->fd);
        }
    }
#else
    (void)wait;
#endif
    return Rf_ScalarReal(flushed);
}

// Evict (advice 0 = MADV_DONTNEED) or read-ahead-hint (1 = MADV_SEQUENTIAL,
// 2 = MADV_WILLNEED) the whole payload of an fmalloc vector. Returns the
// number of bytes advised. Mainly for benchmarking cold-cache behavior.
extern "C" SEXP rfm_vector_advise_impl(SEXP vector_x, SEXP advice_sexp)
{
    fm_vector *vec = maybe_vector_from_altrep(vector_x);
    if (!vec) {
        Rf_error("x is not an fmalloc ALTREP vector");
    }
    int code = Rf_asInteger(advice_sexp);
    ooc_advice_t advice = code == 1 ? OOC_SEQUENTIAL : code == 2 ? OOC_WILLNEED : OOC_DONTNEED;
    if (vec->data && vec->bytes > 0) {
        ooc_advise(vec->data, vec->bytes, advice);
    }
    return Rf_ScalarReal((double)vec->bytes);
}

// Fill an fmalloc raw vector by repeating a byte pattern. Useful for building
// large typed-tensor payloads (e.g. a constant f16/quantized fill) without a
// giant R intermediate. Returns the number of bytes written.
extern "C" SEXP rfm_raw_fill_pattern_impl(SEXP vector_x, SEXP pattern_sexp)
{
    fm_vector *vec = maybe_vector_from_altrep(vector_x);
    if (!vec || vec->type != RAWSXP) {
        Rf_error("x must be an fmalloc raw vector");
    }
    if (TYPEOF(pattern_sexp) != RAWSXP || XLENGTH(pattern_sexp) == 0) {
        Rf_error("pattern must be a non-empty raw vector");
    }
    if (!vec->runtime || !vec->runtime->info) {
        Rf_error("fmalloc runtime is closed");
    }

    uint8_t *dst = static_cast<uint8_t *>(vector_data_or_dummy(vec));
    const uint8_t *pat = RAW(pattern_sexp);
    size_t plen = (size_t)XLENGTH(pattern_sexp);
    size_t total = (size_t)vec->len;
    for (size_t i = 0; i < total; i++) {
        dst[i] = pat[i % plen];
        if ((i & 0xFFFFFFF) == 0) {
            R_CheckUserInterrupt();
        }
    }
    vec->maybe_dirty = true;
    return Rf_ScalarReal((double)total);
}

extern "C" SEXP rfm_matmul_ooc_impl(SEXP a_x, SEXP x_dense, SEXP tile_bytes_sexp)
{
    fm_vector *a_vec = maybe_vector_from_altrep(a_x);
    if (!a_vec || a_vec->type != REALSXP) {
        Rf_error("A must be an fmalloc double matrix");
    }
    if (!a_vec->runtime || !a_vec->runtime->info) {
        Rf_error("fmalloc runtime is closed");
    }

    SEXP adim = Rf_getAttrib(a_x, R_DimSymbol);
    if (adim == R_NilValue || TYPEOF(adim) != INTSXP || XLENGTH(adim) != 2) {
        Rf_error("A must be a matrix");
    }
    R_xlen_t m = (R_xlen_t)INTEGER(adim)[0];
    R_xlen_t n = (R_xlen_t)INTEGER(adim)[1];

    if (TYPEOF(x_dense) != REALSXP) {
        Rf_error("X must be a double vector or matrix");
    }
    R_xlen_t xnrow, xncol;
    SEXP xdim = Rf_getAttrib(x_dense, R_DimSymbol);
    if (xdim != R_NilValue && TYPEOF(xdim) == INTSXP && XLENGTH(xdim) == 2) {
        xnrow = (R_xlen_t)INTEGER(xdim)[0];
        xncol = (R_xlen_t)INTEGER(xdim)[1];
    } else {
        xnrow = XLENGTH(x_dense);
        xncol = 1;
    }
    if (xnrow != n) {
        Rf_error("non-conformable arguments");
    }
    if (m > (R_xlen_t)std::numeric_limits<int>::max() ||
        n > (R_xlen_t)std::numeric_limits<int>::max() ||
        xncol > (R_xlen_t)std::numeric_limits<int>::max()) {
        Rf_error("dimensions exceed the BLAS integer interface");
    }

    double tile_req = Rf_asReal(tile_bytes_sexp);
    // Default 256 MiB resident per tile; clamp to at least one column.
    size_t tile_bytes = (!R_FINITE(tile_req) || tile_req < 1)
                            ? (size_t)256 << 20
                            : (size_t)tile_req;
    R_xlen_t col_bytes = m * (R_xlen_t)sizeof(double);
    R_xlen_t tile_cols = col_bytes > 0 ? (R_xlen_t)(tile_bytes / (size_t)col_bytes) : n;
    if (tile_cols < 1) {
        tile_cols = 1;
    }

    fm_vector *y_vec = allocate_fm_vector(a_vec->runtime, REALSXP, m * xncol, true, false);
    SEXP ans = PROTECT(fmalloc_new_altrep(y_vec));
    SEXP ydim = PROTECT(Rf_allocVector(INTSXP, 2));
    INTEGER(ydim)[0] = (int)m;
    INTEGER(ydim)[1] = (int)xncol;
    Rf_setAttrib(ans, R_DimSymbol, ydim);

    if (m == 0 || xncol == 0) {
        UNPROTECT(2);
        return ans;
    }

    double *A = static_cast<double *>(vector_data_or_dummy(a_vec));
    double *X = REAL(x_dense);
    double *Y = static_cast<double *>(vector_data_or_dummy(y_vec));

    if (n == 0) {
        memset(Y, 0, (size_t)(m * xncol) * sizeof(double));
        UNPROTECT(2);
        return ans;
    }

    ooc_advise(A, (size_t)(m * n) * sizeof(double), OOC_SEQUENTIAL);

    int mi = (int)m, ki = (int)xncol, ni = (int)n; // ni = ldb of X (n x k)
    for (R_xlen_t j0 = 0; j0 < n; j0 += tile_cols) {
        R_xlen_t tw = n - j0 < tile_cols ? n - j0 : tile_cols;
        int kk = (int)tw;
        double beta = (j0 == 0) ? 0.0 : 1.0;
        double *A_tile = A + j0 * m;
        rfm_gemm("N", "N", mi, ki, kk, 1.0, A_tile, mi, X + j0, ni, beta, Y, mi);
        // Release the tile's pages: resident set stays ~tile_bytes, not m*n*8.
        ooc_advise(A_tile, (size_t)(tw * m) * sizeof(double), OOC_DONTNEED);
        R_CheckUserInterrupt();
    }

    UNPROTECT(2);
    return ans;
}

// Out-of-core tcrossprod: C = X X' for a large column-major fmalloc double
// matrix X (m x n), returning an m x m fmalloc matrix. Tiling is over the
// reduction dimension (columns of X): each contiguous column panel is a
// rank-kw update C += X[,k-panel] X[,k-panel]' via dgemm('N','T'), read once
// and evicted. Single streaming pass over X (input residency ~1 panel); the
// m x m result is fmalloc-backed.
extern "C" SEXP rfm_tcrossprod_ooc_impl(SEXP a_x, SEXP tile_bytes_sexp)
{
    fm_vector *a_vec = maybe_vector_from_altrep(a_x);
    if (!a_vec || a_vec->type != REALSXP) {
        Rf_error("X must be an fmalloc double matrix");
    }
    if (!a_vec->runtime || !a_vec->runtime->info) {
        Rf_error("fmalloc runtime is closed");
    }
    SEXP adim = Rf_getAttrib(a_x, R_DimSymbol);
    if (adim == R_NilValue || TYPEOF(adim) != INTSXP || XLENGTH(adim) != 2) {
        Rf_error("X must be a matrix");
    }
    R_xlen_t m = (R_xlen_t)INTEGER(adim)[0];
    R_xlen_t n = (R_xlen_t)INTEGER(adim)[1];
    if (m > (R_xlen_t)std::numeric_limits<int>::max() ||
        n > (R_xlen_t)std::numeric_limits<int>::max()) {
        Rf_error("dimensions exceed the BLAS integer interface");
    }
    if (m > 0 && m > (R_xlen_t)std::numeric_limits<R_xlen_t>::max() / m) {
        Rf_error("tcrossprod result is too large");
    }

    double tile_req = Rf_asReal(tile_bytes_sexp);
    size_t tile_bytes = (!R_FINITE(tile_req) || tile_req < 1) ? (size_t)256 << 20
                                                              : (size_t)tile_req;
    R_xlen_t col_bytes = m * (R_xlen_t)sizeof(double);
    R_xlen_t pw = col_bytes > 0 ? (R_xlen_t)(tile_bytes / (size_t)col_bytes) : n;
    if (pw < 1) {
        pw = 1;
    }

    fm_vector *c_vec = allocate_fm_vector(a_vec->runtime, REALSXP, m * m, true, false);
    SEXP ans = PROTECT(fmalloc_new_altrep(c_vec));
    SEXP cdim = PROTECT(Rf_allocVector(INTSXP, 2));
    INTEGER(cdim)[0] = (int)m;
    INTEGER(cdim)[1] = (int)m;
    Rf_setAttrib(ans, R_DimSymbol, cdim);

    if (m == 0) {
        UNPROTECT(2);
        return ans;
    }

    double *X = static_cast<double *>(vector_data_or_dummy(a_vec));
    double *C = static_cast<double *>(vector_data_or_dummy(c_vec));

    if (n == 0) {
        memset(C, 0, (size_t)(m * m) * sizeof(double));
        UNPROTECT(2);
        return ans;
    }

    ooc_advise(X, (size_t)(m * n) * sizeof(double), OOC_SEQUENTIAL);

    int mi = (int)m, mc = (int)m; // C is m x m, ldc = m
    for (R_xlen_t k0 = 0; k0 < n; k0 += pw) {
        R_xlen_t kw = n - k0 < pw ? n - k0 : pw;
        int kk = (int)kw;
        double beta = (k0 == 0) ? 0.0 : 1.0;
        double *X_panel = X + k0 * m;
        // C += X[,k0:k1] X[,k0:k1]'
        rfm_gemm("N", "T", mi, mc, kk, 1.0, X_panel, mi, X_panel, mi, beta, C, mi);
        ooc_advise(X_panel, (size_t)(kw * m) * sizeof(double), OOC_DONTNEED);
        R_CheckUserInterrupt();
    }

    UNPROTECT(2);
    return ans;
}

// Out-of-core crossprod: C = X' X for a large column-major fmalloc double
// matrix X (m x n), returning an n x n fmalloc matrix. X is consumed as pairs
// of contiguous column panels: output block (i,j) = X[,i-panel]' X[,j-panel]
// is computed with dgemm straight into C, then both panels' pages are
// released. Input residency stays ~2 panels regardless of X's size, and the
// n x n result is fmalloc-backed so it too may exceed RAM.
/* list(gram, colsums): colsums is NULL unless it was requested. Callers that
 * only want the Gram matrix take [[1]]. */
static SEXP crossprod_ooc_result(SEXP gram, SEXP colsums)
{
    SEXP out = PROTECT(Rf_allocVector(VECSXP, 2));
    SET_VECTOR_ELT(out, 0, gram);
    SET_VECTOR_ELT(out, 1, colsums);
    SEXP nm = PROTECT(Rf_allocVector(STRSXP, 2));
    SET_STRING_ELT(nm, 0, Rf_mkChar("gram"));
    SET_STRING_ELT(nm, 1, Rf_mkChar("colsums"));
    Rf_setAttrib(out, R_NamesSymbol, nm);
    UNPROTECT(2);
    return out;
}

extern "C" SEXP rfm_crossprod_ooc_impl(SEXP a_x, SEXP tile_bytes_sexp,
                                       SEXP colsums_sexp)
{
    fm_vector *a_vec = maybe_vector_from_altrep(a_x);
    if (!a_vec || a_vec->type != REALSXP) {
        Rf_error("X must be an fmalloc double matrix");
    }
    if (!a_vec->runtime || !a_vec->runtime->info) {
        Rf_error("fmalloc runtime is closed");
    }
    SEXP adim = Rf_getAttrib(a_x, R_DimSymbol);
    if (adim == R_NilValue || TYPEOF(adim) != INTSXP || XLENGTH(adim) != 2) {
        Rf_error("X must be a matrix");
    }
    R_xlen_t m = (R_xlen_t)INTEGER(adim)[0];
    R_xlen_t n = (R_xlen_t)INTEGER(adim)[1];
    if (n > (R_xlen_t)std::numeric_limits<int>::max() ||
        m > (R_xlen_t)std::numeric_limits<int>::max()) {
        Rf_error("dimensions exceed the BLAS integer interface");
    }
    if (n > 0 && n > (R_xlen_t)std::numeric_limits<R_xlen_t>::max() / n) {
        Rf_error("crossprod result is too large");
    }

    double tile_req = Rf_asReal(tile_bytes_sexp);
    size_t tile_bytes = (!R_FINITE(tile_req) || tile_req < 1) ? (size_t)256 << 20
                                                              : (size_t)tile_req;
    R_xlen_t col_bytes = m * (R_xlen_t)sizeof(double);
    R_xlen_t pw = col_bytes > 0 ? (R_xlen_t)(tile_bytes / (size_t)col_bytes) : n;
    if (pw < 1) {
        pw = 1;
    }

    /* Column sums of X, accumulated in the same sweep as the Gram matrix when
     * the blocking allows it. Returned alongside C so that a centered Gram
     * (fmalloc_pca) costs one pass over X, not two. */
    const bool want_colsums = (Rf_asLogical(colsums_sexp) == TRUE);

    fm_vector *c_vec = allocate_fm_vector(a_vec->runtime, REALSXP, n * n, true, false);
    SEXP ans = PROTECT(fmalloc_new_altrep(c_vec));
    SEXP cdim = PROTECT(Rf_allocVector(INTSXP, 2));
    INTEGER(cdim)[0] = (int)n;
    INTEGER(cdim)[1] = (int)n;
    Rf_setAttrib(ans, R_DimSymbol, cdim);

    SEXP cs = PROTECT(want_colsums ? Rf_allocVector(REALSXP, n) : R_NilValue);
    /* Accumulate in long double, as R's own colSums does, so that the fused sums
     * agree bit for bit with the separate reduction the caller would otherwise
     * have run: which path computed them must not be observable. n is moderate
     * by construction here (the Gram is formed in memory). R_alloc is released
     * when this .Call returns. */
    long double *colsums = want_colsums
        ? (long double *)R_alloc((size_t)n, sizeof(long double))
        : nullptr;
    if (colsums) {
        for (R_xlen_t j = 0; j < n; j++) {
            colsums[j] = 0.0L;
        }
    }

    if (n == 0) {
        SEXP out = PROTECT(crossprod_ooc_result(ans, cs));
        UNPROTECT(4);
        return out;
    }

    double *X = static_cast<double *>(vector_data_or_dummy(a_vec));
    double *C = static_cast<double *>(vector_data_or_dummy(c_vec));

    if (m == 0) {
        memset(C, 0, (size_t)(n * n) * sizeof(double));
        SEXP out = PROTECT(crossprod_ooc_result(ans, cs));
        UNPROTECT(4);
        return out;
    }

    ooc_advise(X, (size_t)(m * n) * sizeof(double), OOC_SEQUENTIAL);

    int mi = (int)m, nc = (int)n; // nc = ldc of the n x n result

    /*
     * Two blockings, opposite I/O tradeoffs. Both do n*n*m flops; what differs
     * is how many times each byte crosses the memory bus, which is what this
     * kernel is actually bound by (measured: time scales with the pass count at
     * constant flops, at a flat ~5 GB/s).
     *
     *   row blocks     read X once, update the whole n x n C once per block
     *   column panels  write each C block once, re-read all of X per i-panel
     *
     * So the choice is |X| vs |C|: stream the big one, keep the small one hot.
     * When C fits the tile budget (the documented case for fmalloc_pca: n
     * moderate after feature selection, m huge) row blocking moves ceil(n/pw)
     * times less data. A tall X makes that ratio brutal, because pw shrinks as
     * m grows: m = 1e7, n = 2000, 256 MB tile gives pw = 3, i.e. 667 passes.
     */
    const R_xlen_t gram_bytes = n * n * (R_xlen_t)sizeof(double);
    if ((size_t)gram_bytes <= tile_bytes) {
        /* Row blocks: C += Xb'Xb, Xb = X[i0:i1, ] (rb x n, leading dim m).
         * Each element of X is read exactly once. C stays resident. */
        R_xlen_t rb = (R_xlen_t)(tile_bytes / ((size_t)n * sizeof(double)));
        if (rb < 1) {
            rb = 1;
        }
        double beta = 0.0;
        for (R_xlen_t i0 = 0; i0 < m; i0 += rb) {
            R_xlen_t kw = m - i0 < rb ? m - i0 : rb;
            if (colsums) {
                /* Fused: the block's pages are resident for the gemm anyway, so
                 * the column sums cost one add per element on a pass we are
                 * already paying for. Summing i within j, blocks in order, is
                 * the same association as a single sequential colSums. */
                for (R_xlen_t j = 0; j < n; j++) {
                    const double *col = X + j * m + i0;
                    long double s = colsums[j];
                    for (R_xlen_t i = 0; i < kw; i++) {
                        s += col[i];
                    }
                    colsums[j] = s;
                }
            }
            rfm_gemm("T", "N", nc, nc, (int)kw, 1.0, X + i0, mi, X + i0, mi,
                     beta, C, nc);
            beta = 1.0;
            if (i0 + kw < m) {
                for (R_xlen_t j = 0; j < n; j++) {
                    ooc_advise(X + j * m + i0, (size_t)kw * sizeof(double),
                               OOC_DONTNEED);
                }
            }
            R_CheckUserInterrupt();
        }
    } else {
        /* Column panels: C too large to hold, so write each C block once and
         * pay to re-read X. For each row-panel i of C, hold panel X[,i0:i1]
         * resident and sweep the column panels j. Input residency ~2 panels. */
        for (R_xlen_t i0 = 0; i0 < n; i0 += pw) {
            R_xlen_t iw = n - i0 < pw ? n - i0 : pw;
            int ii = (int)iw;
            for (R_xlen_t j0 = 0; j0 < n; j0 += pw) {
                R_xlen_t jw = n - j0 < pw ? n - j0 : pw;
                int jj = (int)jw;
                rfm_gemm("T", "N", ii, jj, mi, 1.0, X + i0 * m, mi, X + j0 * m, mi,
                         0.0, C + i0 + j0 * n, nc);
                if (j0 != i0) {
                    ooc_advise(X + j0 * m, (size_t)(jw * m) * sizeof(double), OOC_DONTNEED);
                }
                R_CheckUserInterrupt();
            }
            ooc_advise(X + i0 * m, (size_t)(iw * m) * sizeof(double), OOC_DONTNEED);
        }
        if (colsums) {
            /* No free ride here: X is read many times but never wholly resident
             * in a shape we can reduce along. Take one honest extra pass. */
            for (R_xlen_t j = 0; j < n; j++) {
                const double *col = X + j * m;
                long double s = 0.0L;
                for (R_xlen_t i = 0; i < m; i++) {
                    s += col[i];
                }
                colsums[j] = s;
                R_CheckUserInterrupt();
            }
        }
    }

    if (want_colsums) {
        for (R_xlen_t j = 0; j < n; j++) {
            REAL(cs)[j] = (double)colsums[j];
        }
    }
    SEXP out = PROTECT(crossprod_ooc_result(ans, cs));
    UNPROTECT(4);
    return out;
}
