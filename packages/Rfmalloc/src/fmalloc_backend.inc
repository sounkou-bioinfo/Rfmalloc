//==============================================================================
// Pluggable matrix-multiply backend registry.
//
// The matrix-product kernels call rfm_gemm() instead of dgemm directly.
// rfm_gemm dispatches to a registered backend when one is selected, otherwise
// to R's BLAS. Downstream packages register a backend (e.g. a GPU cuBLAS
// kernel, or an out-of-core-aware GEMM) through the Rfmalloc_register_matmul_-
// backend C-callable and select it by name; a backend may decline a call
// (return non-zero) to fall back to BLAS. This is Rfmalloc-scoped (base R's
// %*% is unaffected) and needs no recompile of Rfmalloc - the same ergonomics
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

// Second-tier, codec-aware hook: multiply a compressed/typed tensor by a dense
// double operand WITHOUT decoding it to f64 first. The backend receives the
// raw codec payload (byte-identical to how the codec stored it - e.g. an
// fmalloc-mmap'd q4_k payload is a valid ggml Q4_K tensor) plus the dims, so a
// device/quantized engine can do native quantized matmul and keep the weight
// resident. C = T x D (typed_on_left) or D x T (else). Returns 0 if handled,
// non-zero to decline (Rfmalloc then decodes panels and uses the gemm path).
typedef int (*rfm_typed_gemm_fn)(const char *codec,
                                 const void *payload, size_t payload_bytes,
                                 int tensor_nrow, int tensor_ncol,
                                 int typed_on_left,
                                 const double *dense, int dense_nrow, int dense_ncol,
                                 double *C);

struct rfm_matmul_backend {
    char name[32];
    rfm_gemm_fn fn;
    rfm_typed_gemm_fn typed_fn; // optional; nullptr if the backend is dense-only
};

#define RFM_MAX_BACKENDS 16
static rfm_matmul_backend matmul_backends[RFM_MAX_BACKENDS];
static int matmul_backend_count = 0;
static rfm_gemm_fn active_backend_fn = nullptr;
static rfm_typed_gemm_fn active_typed_fn = nullptr;
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

// Register a backend with an optional dense gemm_fn and an optional codec-aware
// typed_fn (at least one must be non-null).
extern "C" int Rfmalloc_register_matmul_backend_ex(const char *name, rfm_gemm_fn fn,
                                                   rfm_typed_gemm_fn typed_fn)
{
    if (!name || !name[0] || strlen(name) >= sizeof(matmul_backends[0].name) ||
        (!fn && !typed_fn)) {
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
    b->typed_fn = typed_fn;
    matmul_backend_count++;
    return 0;
}

extern "C" int Rfmalloc_register_matmul_backend(const char *name, rfm_gemm_fn fn)
{
    return Rfmalloc_register_matmul_backend_ex(name, fn, nullptr);
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

// Typed (codec-aware) dispatch. Returns 1 if the active backend handled the
// compressed product directly, 0 otherwise (caller decodes panels + gemm).
static int rfm_typed_gemm(const char *codec, const void *payload, size_t payload_bytes,
                          int tensor_nrow, int tensor_ncol, int typed_on_left,
                          const double *dense, int dense_nrow, int dense_ncol, double *C)
{
    if (!active_typed_fn) {
        return 0;
    }
    return active_typed_fn(codec, payload, payload_bytes, tensor_nrow, tensor_ncol,
                           typed_on_left, dense, dense_nrow, dense_ncol, C) == 0 ? 1 : 0;
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
        active_typed_fn = nullptr;
        active_backend_name[0] = '\0';
        return Rf_mkString("blas");
    }
    const rfm_matmul_backend *b = matmul_backend_lookup(name);
    if (!b) {
        Rf_error("no registered matmul backend named '%s'", name);
    }
    active_backend_fn = b->fn;
    active_typed_fn = b->typed_fn;
    strcpy(active_backend_name, b->name);
    return Rf_mkString(active_backend_name);
}

extern "C" SEXP rfm_matmul_backend_impl(void)
{
    return Rf_mkString(active_backend_name[0] ? active_backend_name : "blas");
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

// Test-only typed backend: consumes the raw "f32" codec payload directly
// (widening f32 -> f64 itself, i.e. no Rfmalloc-side decode), computes
// 2 * (typed x dense) or 2 * (dense x typed), and returns 0. Proves the
// codec-aware hook receives the compressed payload and dispatch reaches it.
static int rfm_test_typed_f32_scale2(const char *codec,
                                     const void *payload, size_t payload_bytes,
                                     int tnrow, int tncol, int typed_on_left,
                                     const double *dense, int dnrow, int dncol, double *C)
{
    if (strcmp(codec, "f32") != 0 ||
        payload_bytes < (size_t)tnrow * tncol * sizeof(float)) {
        return 1; // decline anything but f32 -> Rfmalloc falls back
    }
    const float *A = static_cast<const float *>(payload);
    std::vector<double> Ad((size_t)tnrow * tncol);
    for (size_t i = 0; i < Ad.size(); i++) {
        Ad[i] = (double)A[i];
    }
    const double one = 1.0, zero = 0.0, two = 2.0;
    if (typed_on_left) {
        // C(tnrow x dncol) = 2 * T(tnrow x tncol) x D(tncol x dncol)
        int m = tnrow, n = dncol, k = tncol, lda = tnrow, ldb = dnrow, ldc = tnrow;
        F77_CALL(dgemm)("N", "N", &m, &n, &k, &two, Ad.data(), &lda,
                        const_cast<double *>(dense), &ldb, &zero, C, &ldc FCONE FCONE);
    } else {
        // C(dnrow x tncol) = 2 * D(dnrow x tnrow) x T(tnrow x tncol)
        int m = dnrow, n = tncol, k = tnrow, lda = dnrow, ldb = tnrow, ldc = dnrow;
        F77_CALL(dgemm)("N", "N", &m, &n, &k, &two, const_cast<double *>(dense), &lda,
                        Ad.data(), &ldb, &zero, C, &ldc FCONE FCONE);
    }
    (void)one;
    return 0;
}

extern "C" SEXP rfm_register_test_typed_backend_impl(void)
{
    return Rf_ScalarInteger(Rfmalloc_register_matmul_backend_ex(
        "test_typed_f32", nullptr, rfm_test_typed_f32_scale2));
}
