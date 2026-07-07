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

// madvise()/MADV_* are POSIX but absent on some platforms — the OOC path still
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
// without madvise). Failures are ignored — they are expected for non-mmap or
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

    const double one = 1.0, zero = 0.0;
    int mi = (int)m, ki = (int)xncol, ni = (int)n; // ni = ldb of X (n x k)
    for (R_xlen_t j0 = 0; j0 < n; j0 += tile_cols) {
        R_xlen_t tw = n - j0 < tile_cols ? n - j0 : tile_cols;
        int kk = (int)tw;
        const double *beta = (j0 == 0) ? &zero : &one;
        double *A_tile = A + j0 * m;
        F77_CALL(dgemm)("N", "N", &mi, &ki, &kk, &one,
                        A_tile, &mi, X + j0, &ni, beta,
                        Y, &mi FCONE FCONE);
        // Release the tile's pages: resident set stays ~tile_bytes, not m*n*8.
        ooc_advise(A_tile, (size_t)(tw * m) * sizeof(double), OOC_DONTNEED);
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
extern "C" SEXP rfm_crossprod_ooc_impl(SEXP a_x, SEXP tile_bytes_sexp)
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

    fm_vector *c_vec = allocate_fm_vector(a_vec->runtime, REALSXP, n * n, true, false);
    SEXP ans = PROTECT(fmalloc_new_altrep(c_vec));
    SEXP cdim = PROTECT(Rf_allocVector(INTSXP, 2));
    INTEGER(cdim)[0] = (int)n;
    INTEGER(cdim)[1] = (int)n;
    Rf_setAttrib(ans, R_DimSymbol, cdim);

    if (n == 0) {
        UNPROTECT(2);
        return ans;
    }

    double *X = static_cast<double *>(vector_data_or_dummy(a_vec));
    double *C = static_cast<double *>(vector_data_or_dummy(c_vec));

    if (m == 0) {
        memset(C, 0, (size_t)(n * n) * sizeof(double));
        UNPROTECT(2);
        return ans;
    }

    ooc_advise(X, (size_t)(m * n) * sizeof(double), OOC_SEQUENTIAL);

    const double one = 1.0, zero = 0.0;
    int mi = (int)m, nc = (int)n; // nc = ldc of the n x n result
    // For each row-panel i of C, hold panel X[,i0:i1] resident and sweep the
    // column panels j, writing each block C[i0:i1, j0:j1] = X[,i0:i1]'X[,j0:j1]
    // straight into the result. Input residency stays ~2 panels.
    for (R_xlen_t i0 = 0; i0 < n; i0 += pw) {
        R_xlen_t iw = n - i0 < pw ? n - i0 : pw;
        int ii = (int)iw;
        for (R_xlen_t j0 = 0; j0 < n; j0 += pw) {
            R_xlen_t jw = n - j0 < pw ? n - j0 : pw;
            int jj = (int)jw;
            F77_CALL(dgemm)("T", "N", &ii, &jj, &mi, &one,
                            X + i0 * m, &mi, X + j0 * m, &mi, &zero,
                            C + i0 + j0 * n, &nc FCONE FCONE);
            if (j0 != i0) {
                ooc_advise(X + j0 * m, (size_t)(jw * m) * sizeof(double), OOC_DONTNEED);
            }
            R_CheckUserInterrupt();
        }
        ooc_advise(X + i0 * m, (size_t)(iw * m) * sizeof(double), OOC_DONTNEED);
    }

    UNPROTECT(2);
    return ans;
}
