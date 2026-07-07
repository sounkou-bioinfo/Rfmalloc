/*
 * rggml_test.c - .Call() entry points used only by the tinytest smoke test.
 *
 * These intentionally go through the *installed* Rggml.h header and its
 * R_GetCCallable() accessors, exactly as a downstream package would, rather
 * than calling the static rggml_api.c functions directly - this is what
 * proves the registered C-callable path actually works end to end.
 */

#include <stdlib.h>
#include <stdint.h>
#include <time.h>

#include <R.h>
#include <Rinternals.h>

#include <Rggml.h>

SEXP RC_rggml_version(void)
{
    Rggml_version_fun version_fn = Rggml_version_ptr();
    if (!version_fn) Rf_error("Rggml_version C-callable not registered");
    return Rf_mkString(version_fn());
}

/*
 * RC_rggml_test_q4k_dot(nblocks)
 *
 * Exercises the runtime-SIMD-dispatched q4_K x q8_K dot product. Builds
 * deterministic inputs of length nblocks*256, quantizes them to Q4_K and Q8_K
 * with GGML's own quantizers, then calls both the canonical (dispatched, i.e.
 * AVX2/NEON where staged) ggml_vec_dot_q4_K_q8_K and GGML's scalar reference
 * ggml_vec_dot_q4_K_q8_K_generic on identical bytes. Returns c(dispatched,
 * scalar); the tinytest asserts they agree, proving the staged ISA variant is
 * correct. These are GGML-internal symbols (not C-callables); this test file
 * is part of the package and links libggml.a directly, so it declares them.
 */
extern void quantize_row_q8_K(const float *x, void *y, int64_t k);
extern void ggml_vec_dot_q4_K_q8_K(int n, float *s, size_t bs,
        const void *vx, size_t bx, const void *vy, size_t by, int nrc);
extern void ggml_vec_dot_q4_K_q8_K_generic(int n, float *s, size_t bs,
        const void *vx, size_t bx, const void *vy, size_t by, int nrc);

SEXP RC_rggml_test_q4k_dot(SEXP nblocks_sexp)
{
    int nb = Rf_asInteger(nblocks_sexp);
    if (nb < 1) nb = 1;
    const int QKK = 256;
    int n = nb * QKK;

    /* Initializing the CPU backend runs ggml_cpu_init(), which populates the
     * fp16->fp32 lookup table the q4_K dot uses to read block scales. Without
     * it those scales read as 0 and the dot is (silently) 0. */
    Rggml_backend_cpu_init_fun backend_init = Rggml_backend_cpu_init_ptr();
    Rggml_backend_free_fun     backend_free = Rggml_backend_free_ptr();
    ggml_backend_t cpu = backend_init ? backend_init() : NULL;

    /* Deterministic pseudo-random inputs (LCG), no RNG-state dependency. */
    float *x = (float *) R_alloc((size_t) n, sizeof(float));
    float *y = (float *) R_alloc((size_t) n, sizeof(float));
    uint32_t st = 0x9e3779b9u;
    for (int i = 0; i < n; i++) {
        st = st * 1664525u + 1013904223u;
        x[i] = (float) ((int32_t) (st >> 8) / 8388608.0 - 1.0);   /* ~[-1,1) */
        st = st * 1664525u + 1013904223u;
        y[i] = (float) ((int32_t) (st >> 8) / 8388608.0 - 1.0);
    }

    size_t xb = ggml_row_size(GGML_TYPE_Q4_K, n);
    size_t yb = ggml_row_size(GGML_TYPE_Q8_K, n);
    void *qx = (void *) R_alloc(xb, 1);
    void *qy = (void *) R_alloc(yb, 1);

    ggml_quantize_chunk(GGML_TYPE_Q4_K, x, qx, 0, 1, n, NULL);
    quantize_row_q8_K(y, qy, n);

    float s_disp = 0.0f, s_gen = 0.0f;
    ggml_vec_dot_q4_K_q8_K(n, &s_disp, 0, qx, 0, qy, 0, 1);
    ggml_vec_dot_q4_K_q8_K_generic(n, &s_gen, 0, qx, 0, qy, 0, 1);

    if (cpu && backend_free) backend_free(cpu);

    SEXP out = PROTECT(Rf_allocVector(REALSXP, 2));
    REAL(out)[0] = (double) s_disp;
    REAL(out)[1] = (double) s_gen;
    UNPROTECT(1);
    return out;
}

/*
 * RC_rggml_bench_q4k_dot(nblocks, iters) - time the dispatched (staged ISA)
 * q4_K dot against GGML's scalar reference over `iters` repetitions on the
 * same quantized inputs. Returns c(dispatched_sec, scalar_sec). Not a
 * regression test (timings are machine-dependent); a helper to report the
 * SIMD speedup.
 */
static double rggml_now_sec(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double) ts.tv_sec + (double) ts.tv_nsec * 1e-9;
}

SEXP RC_rggml_bench_q4k_dot(SEXP nblocks_sexp, SEXP iters_sexp)
{
    int nb = Rf_asInteger(nblocks_sexp);
    if (nb < 1) nb = 1;
    int iters = Rf_asInteger(iters_sexp);
    if (iters < 1) iters = 1;
    const int QKK = 256;
    int n = nb * QKK;

    Rggml_backend_cpu_init_fun backend_init = Rggml_backend_cpu_init_ptr();
    Rggml_backend_free_fun     backend_free = Rggml_backend_free_ptr();
    ggml_backend_t cpu = backend_init ? backend_init() : NULL;

    float *x = (float *) R_alloc((size_t) n, sizeof(float));
    float *y = (float *) R_alloc((size_t) n, sizeof(float));
    uint32_t st = 0x9e3779b9u;
    for (int i = 0; i < n; i++) {
        st = st * 1664525u + 1013904223u;
        x[i] = (float) ((int32_t) (st >> 8) / 8388608.0 - 1.0);
        st = st * 1664525u + 1013904223u;
        y[i] = (float) ((int32_t) (st >> 8) / 8388608.0 - 1.0);
    }
    void *qx = (void *) R_alloc(ggml_row_size(GGML_TYPE_Q4_K, n), 1);
    void *qy = (void *) R_alloc(ggml_row_size(GGML_TYPE_Q8_K, n), 1);
    ggml_quantize_chunk(GGML_TYPE_Q4_K, x, qx, 0, 1, n, NULL);
    quantize_row_q8_K(y, qy, n);

    volatile float sink = 0.0f;
    float s;
    double t0 = rggml_now_sec();
    for (int it = 0; it < iters; it++) { ggml_vec_dot_q4_K_q8_K(n, &s, 0, qx, 0, qy, 0, 1); sink += s; }
    double t_disp = rggml_now_sec() - t0;

    t0 = rggml_now_sec();
    for (int it = 0; it < iters; it++) { ggml_vec_dot_q4_K_q8_K_generic(n, &s, 0, qx, 0, qy, 0, 1); sink += s; }
    double t_gen = rggml_now_sec() - t0;

    if (cpu && backend_free) backend_free(cpu);

    SEXP out = PROTECT(Rf_allocVector(REALSXP, 2));
    REAL(out)[0] = t_disp;
    REAL(out)[1] = t_gen;
    UNPROTECT(1);
    (void) sink;
    return out;
}

/*
 * RC_rggml_test_mul_mat(A, B, zero_copy)
 *
 * A, B: numeric matrices, nrow(A) == nrow(B) == the contracted dimension.
 * zero_copy: if TRUE, tensors are created in a no_alloc = 1 context and
 *   pointed at caller-owned float buffers (the mmap-style path); if FALSE,
 *   tensors are created in a normal (no_alloc = 0) context and filled via
 *   the ggml-owned data pointer.
 *
 * Returns a numeric matrix, dim (ncol(A), ncol(B)), computed via
 * ggml_mul_mat(ctx, A, B) on the CPU backend. Verified/documented
 * convention (see README.md / man/ggml_version.Rd): reinterpreting each
 * input tensor's raw column-major memory directly as an R matrix of dim
 * (ne[0], ne[1]), the result (dim (A_ne1, B_ne1)) equals crossprod(A, B),
 * i.e. t(A) %*% B.
 */
SEXP RC_rggml_test_mul_mat(SEXP A_sexp, SEXP B_sexp, SEXP zero_copy_sexp, SEXP use_blas_sexp)
{
    if (TYPEOF(A_sexp) != REALSXP || TYPEOF(B_sexp) != REALSXP) {
        Rf_error("A and B must be numeric matrices");
    }
    SEXP dimA = Rf_getAttrib(A_sexp, R_DimSymbol);
    SEXP dimB = Rf_getAttrib(B_sexp, R_DimSymbol);
    if (Rf_length(dimA) != 2 || Rf_length(dimB) != 2) {
        Rf_error("A and B must be matrices");
    }
    int64_t kA = INTEGER(dimA)[0];
    int64_t nA = INTEGER(dimA)[1];
    int64_t kB = INTEGER(dimB)[0];
    int64_t nB = INTEGER(dimB)[1];
    if (kA != kB) {
        Rf_error("nrow(A) must equal nrow(B)");
    }
    int zero_copy = Rf_asLogical(zero_copy_sexp);
    int use_blas  = Rf_asLogical(use_blas_sexp);

    Rggml_context_create_fun    ctx_create      = Rggml_context_create_ptr();
    Rggml_context_free_fun      ctx_free        = Rggml_context_free_ptr();
    Rggml_new_tensor_fun        new_tensor      = Rggml_new_tensor_ptr();
    Rggml_tensor_data_fun       tensor_data     = Rggml_tensor_data_ptr();
    Rggml_backend_cpu_init_fun  backend_init    = Rggml_backend_cpu_init_ptr();
    Rggml_backend_blas_init_fun blas_init       = Rggml_backend_blas_init_ptr();
    Rggml_backend_free_fun      backend_free    = Rggml_backend_free_ptr();
    Rggml_compute_mul_mat_fun   compute_mul_mat = Rggml_compute_mul_mat_ptr();
    Rggml_tensor_overhead_fun   tensor_overhead = Rggml_tensor_overhead_ptr();
    Rggml_graph_overhead_fun    graph_overhead  = Rggml_graph_overhead_ptr();

    if (!ctx_create || !ctx_free || !new_tensor || !tensor_data || !backend_init ||
        !backend_free || !compute_mul_mat || !tensor_overhead || !graph_overhead) {
        Rf_error("one or more Rggml C-callables were not found via R_GetCCallable()");
    }

    /* In the no_alloc = 0 path the two input tensors' *data* also lives in the
     * context pool, so size it to include their bytes (plus per-tensor alignment
     * slack); the zero_copy path keeps tensor data in external buffers. */
    size_t data_bytes = zero_copy ? 0
        : ((size_t)kA * (size_t)nA + (size_t)kB * (size_t)nB) * sizeof(float);
    size_t mem_size = (size_t)4 * tensor_overhead() + graph_overhead(8) + data_bytes + 8192;
    struct ggml_context *ctx = ctx_create(mem_size, zero_copy ? 1 : 0);
    if (!ctx) Rf_error("Rggml_context_create (ggml_init) failed");

    int64_t neA[2] = { kA, nA };
    int64_t neB[2] = { kB, nB };

    double *Ap = REAL(A_sexp);
    double *Bp = REAL(B_sexp);
    R_xlen_t nElemA = (R_xlen_t)kA * (R_xlen_t)nA;
    R_xlen_t nElemB = (R_xlen_t)kB * (R_xlen_t)nB;

    float *ext_A = NULL, *ext_B = NULL;
    struct ggml_tensor *tA, *tB;

    if (zero_copy) {
        /* Caller-owned buffers, e.g. standing in for an mmap'd payload the
         * downstream package does not want ggml to copy. */
        ext_A = (float *) malloc(sizeof(float) * (size_t) nElemA);
        ext_B = (float *) malloc(sizeof(float) * (size_t) nElemB);
        if (!ext_A || !ext_B) {
            free(ext_A); free(ext_B);
            ctx_free(ctx);
            Rf_error("allocation failure preparing zero-copy buffers");
        }
        for (R_xlen_t i = 0; i < nElemA; i++) ext_A[i] = (float) Ap[i];
        for (R_xlen_t i = 0; i < nElemB; i++) ext_B[i] = (float) Bp[i];

        tA = new_tensor(ctx, GGML_TYPE_F32, 2, neA, ext_A);
        tB = new_tensor(ctx, GGML_TYPE_F32, 2, neB, ext_B);
    } else {
        tA = new_tensor(ctx, GGML_TYPE_F32, 2, neA, NULL);
        tB = new_tensor(ctx, GGML_TYPE_F32, 2, neB, NULL);
        if (tA && tB) {
            float *tAd = (float *) tensor_data(tA);
            float *tBd = (float *) tensor_data(tB);
            for (R_xlen_t i = 0; i < nElemA; i++) tAd[i] = (float) Ap[i];
            for (R_xlen_t i = 0; i < nElemB; i++) tBd[i] = (float) Bp[i];
        }
    }

    if (!tA || !tB) {
        free(ext_A); free(ext_B);
        ctx_free(ctx);
        Rf_error("Rggml_new_tensor failed");
    }

    ggml_backend_t backend = use_blas ? (blas_init ? blas_init() : NULL) : backend_init();
    if (!backend) {
        free(ext_A); free(ext_B);
        ctx_free(ctx);
        Rf_error(use_blas ? "Rggml_backend_blas_init failed" : "Rggml_backend_cpu_init failed");
    }

    SEXP result = PROTECT(Rf_allocMatrix(REALSXP, (int) nA, (int) nB));
    int rc = compute_mul_mat(ctx, backend, tA, tB, NULL, REAL(result));

    backend_free(backend);
    free(ext_A);
    free(ext_B);
    ctx_free(ctx);

    if (rc != 0) {
        UNPROTECT(1);
        Rf_error("Rggml_compute_mul_mat failed with status %d", rc);
    }

    UNPROTECT(1);
    return result;
}
