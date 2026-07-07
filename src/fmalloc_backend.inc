//==============================================================================
// Pluggable matrix-multiply backend registry.
//
// The matrix-product kernels call rfm_gemm() instead of dgemm directly.
// rfm_gemm dispatches to a registered backend when one is selected, otherwise
// to R's BLAS. Downstream packages register a backend (e.g. a GPU cuBLAS
// kernel, or an out-of-core-aware GEMM) through the Rfmalloc_register_matmul_-
// backend C-callable and select it by name; a backend may decline a call
// (return non-zero) to fall back to BLAS. This is Rfmalloc-scoped (base R's
// %*% is unaffected) and needs no recompile of Rfmalloc — the same ergonomics
// as the tensor codec registry.
//
// Backend signature (by value for easy downstream implementation):
//   int fn(transa, transb, m, n, k, alpha, A, lda, B, ldb, beta, C, ldc)
// computing C = alpha op(A) op(B) + beta C, returning 0 if handled.
//==============================================================================

typedef int (*rfm_gemm_fn)(const char *transa, const char *transb,
                           int m, int n, int k, double alpha,
                           const double *A, int lda, const double *B, int ldb,
                           double beta, double *C, int ldc);

struct rfm_matmul_backend {
    char name[32];
    rfm_gemm_fn fn;
};

#define RFM_MAX_BACKENDS 16
static rfm_matmul_backend matmul_backends[RFM_MAX_BACKENDS];
static int matmul_backend_count = 0;
static rfm_gemm_fn active_backend_fn = nullptr;
static char active_backend_name[32] = "";

static const rfm_matmul_backend *matmul_backend_lookup(const char *name)
{
    for (int i = 0; i < matmul_backend_count; i++) {
        if (strcmp(matmul_backends[i].name, name) == 0) {
            return &matmul_backends[i];
        }
    }
    return nullptr;
}

extern "C" int Rfmalloc_register_matmul_backend(const char *name, rfm_gemm_fn fn)
{
    if (!name || !name[0] || strlen(name) >= sizeof(matmul_backends[0].name) || !fn) {
        return -1;
    }
    if (strcmp(name, "blas") == 0) {
        return -2; // reserved for the default BLAS path
    }
    if (matmul_backend_lookup(name)) {
        return -3;
    }
    if (matmul_backend_count >= RFM_MAX_BACKENDS) {
        return -4;
    }
    rfm_matmul_backend *b = &matmul_backends[matmul_backend_count];
    strcpy(b->name, name);
    b->fn = fn;
    matmul_backend_count++;
    return 0;
}

// The kernels' single entry point. Uses the active backend if it handles the
// call, otherwise R's BLAS dgemm.
static void rfm_gemm(const char *transa, const char *transb,
                     int m, int n, int k, double alpha,
                     const double *A, int lda, const double *B, int ldb,
                     double beta, double *C, int ldc)
{
    if (active_backend_fn) {
        if (active_backend_fn(transa, transb, m, n, k, alpha,
                              A, lda, B, ldb, beta, C, ldc) == 0) {
            return;
        }
        // backend declined -> fall through to BLAS
    }
    int mm = m, nn = n, kk = k, la = lda, lb = ldb, lc = ldc;
    double al = alpha, be = beta;
    F77_CALL(dgemm)(transa, transb, &mm, &nn, &kk, &al,
                    const_cast<double *>(A), &la, const_cast<double *>(B), &lb,
                    &be, C, &lc FCONE FCONE);
}

//==============================================================================
// R-facing selection/introspection
//==============================================================================

extern "C" SEXP rfm_set_matmul_backend_impl(SEXP name_sexp)
{
    if (TYPEOF(name_sexp) != STRSXP || XLENGTH(name_sexp) != 1) {
        Rf_error("backend name must be a single string");
    }
    const char *name = CHAR(STRING_ELT(name_sexp, 0));
    if (name[0] == '\0' || strcmp(name, "blas") == 0) {
        active_backend_fn = nullptr;
        active_backend_name[0] = '\0';
        return Rf_mkString("blas");
    }
    const rfm_matmul_backend *b = matmul_backend_lookup(name);
    if (!b) {
        Rf_error("no registered matmul backend named '%s'", name);
    }
    active_backend_fn = b->fn;
    strcpy(active_backend_name, b->name);
    return Rf_mkString(active_backend_name);
}

extern "C" SEXP rfm_matmul_backend_impl(void)
{
    return Rf_mkString(active_backend_fn ? active_backend_name : "blas");
}

extern "C" SEXP rfm_matmul_backends_impl(void)
{
    SEXP ans = PROTECT(Rf_allocVector(STRSXP, matmul_backend_count));
    for (int i = 0; i < matmul_backend_count; i++) {
        SET_STRING_ELT(ans, i, Rf_mkChar(matmul_backends[i].name));
    }
    UNPROTECT(1);
    return ans;
}

// Test-only backend: computes 2 * (alpha op(A) op(B)) + beta C, so a test can
// select it and verify the result is exactly doubled (proving downstream
// registration + dispatch route through the backend).
static int rfm_test_backend_scale2(const char *transa, const char *transb,
                                   int m, int n, int k, double alpha,
                                   const double *A, int lda, const double *B, int ldb,
                                   double beta, double *C, int ldc)
{
    int mm = m, nn = n, kk = k, la = lda, lb = ldb, lc = ldc;
    double al = alpha * 2.0, be = beta;
    F77_CALL(dgemm)(transa, transb, &mm, &nn, &kk, &al,
                    const_cast<double *>(A), &la, const_cast<double *>(B), &lb,
                    &be, C, &lc FCONE FCONE);
    return 0;
}

extern "C" SEXP rfm_register_test_backend_impl(void)
{
    return Rf_ScalarInteger(
        Rfmalloc_register_matmul_backend("test_scale2", rfm_test_backend_scale2));
}
