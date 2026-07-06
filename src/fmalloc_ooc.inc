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

#include <sys/mman.h>

static inline uintptr_t ooc_page_size(void)
{
    long ps = sysconf(_SC_PAGESIZE);
    return ps > 0 ? (uintptr_t)ps : 4096;
}

// Advise the kernel about the whole A payload (best-effort; ignore failures,
// which are expected for non-mmap or unaligned edge cases).
static void ooc_advise(void *addr, size_t bytes, int advice)
{
    if (!addr || bytes == 0) {
        return;
    }
    uintptr_t ps = ooc_page_size();
    uintptr_t start = (uintptr_t)addr;
    uintptr_t end = start + bytes;
    uintptr_t astart = (start + ps - 1) & ~(ps - 1); // round up
    uintptr_t aend = end & ~(ps - 1);                // round down
    if (aend > astart) {
        madvise((void *)astart, (size_t)(aend - astart), advice);
    }
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
    int advice = code == 1 ? MADV_SEQUENTIAL : code == 2 ? MADV_WILLNEED : MADV_DONTNEED;
    if (vec->data && vec->bytes > 0) {
        ooc_advise(vec->data, vec->bytes, advice);
    }
    return Rf_ScalarReal((double)vec->bytes);
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

    ooc_advise(A, (size_t)(m * n) * sizeof(double), MADV_SEQUENTIAL);

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
        ooc_advise(A_tile, (size_t)(tw * m) * sizeof(double), MADV_DONTNEED);
        R_CheckUserInterrupt();
    }

    UNPROTECT(2);
    return ans;
}
