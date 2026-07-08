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

/*
 * RC_rggml_test_mul_mat_q4k(A, B)
 *
 * The quantized analogue of RC_rggml_test_mul_mat's zero-copy path, and the
 * exact call the Rfmalloc typed-GEMM bridge makes: the weight operand A is
 * quantized to Q4_K into a *separate* heap buffer that stands in for an
 * mmap'd GGUF q4_K payload, and a Q4_K tensor is pointed at it zero-copy
 * (no_alloc context, no ggml-owned copy). The dense F32 activations B are the
 * right operand. ggml_mul_mat() then contracts each Q4_K weight row against
 * B's columns via GGML's type-traits vec_dot for Q4_K - i.e. through the
 * runtime-SIMD-dispatched ggml_vec_dot_q4_K_q8_K (AVX2/NEON where staged) -
 * quantizing B's columns to Q8_K on the fly, exactly as llama.cpp does at
 * inference. This proves the full quantized weight -> compute path (not just
 * the isolated dot) over an external payload.
 *
 * A: numeric weight matrix, nrow(A) = the contracted dimension k, which must
 *    be a multiple of 256 (QK_K); ncol(A) = number of output features.
 * B: numeric activation matrix, nrow(B) = k, ncol(B) = number of columns.
 * Returns a numeric matrix of dim (ncol(A), ncol(B)) = crossprod(A, B) up to
 * q4_K weight + q8_K activation quantization error (same convention as the
 * F32 path). Column-major R storage maps a matrix's column r straight onto
 * ggml row r (ne[0] = k contiguous), so no transpose is needed before
 * quantization.
 */
SEXP RC_rggml_test_mul_mat_q4k(SEXP A_sexp, SEXP B_sexp)
{
    if (TYPEOF(A_sexp) != REALSXP || TYPEOF(B_sexp) != REALSXP) {
        Rf_error("A and B must be numeric matrices");
    }
    SEXP dimA = Rf_getAttrib(A_sexp, R_DimSymbol);
    SEXP dimB = Rf_getAttrib(B_sexp, R_DimSymbol);
    if (Rf_length(dimA) != 2 || Rf_length(dimB) != 2) {
        Rf_error("A and B must be matrices");
    }
    int64_t k  = INTEGER(dimA)[0];
    int64_t nA = INTEGER(dimA)[1];
    int64_t kB = INTEGER(dimB)[0];
    int64_t nB = INTEGER(dimB)[1];
    if (k != kB) {
        Rf_error("nrow(A) must equal nrow(B)");
    }
    if (k % 256 != 0) {
        Rf_error("nrow(A) must be a multiple of 256 (QK_K) to quantize weights to Q4_K; got %lld",
                 (long long) k);
    }

    Rggml_context_create_fun    ctx_create      = Rggml_context_create_ptr();
    Rggml_context_free_fun      ctx_free        = Rggml_context_free_ptr();
    Rggml_new_tensor_fun        new_tensor      = Rggml_new_tensor_ptr();
    Rggml_backend_cpu_init_fun  backend_init    = Rggml_backend_cpu_init_ptr();
    Rggml_backend_free_fun      backend_free    = Rggml_backend_free_ptr();
    Rggml_compute_mul_mat_fun   compute_mul_mat = Rggml_compute_mul_mat_ptr();
    Rggml_tensor_overhead_fun   tensor_overhead = Rggml_tensor_overhead_ptr();
    Rggml_graph_overhead_fun    graph_overhead  = Rggml_graph_overhead_ptr();

    if (!ctx_create || !ctx_free || !new_tensor || !backend_init ||
        !backend_free || !compute_mul_mat || !tensor_overhead || !graph_overhead) {
        Rf_error("one or more Rggml C-callables were not found via R_GetCCallable()");
    }

    /* Initializing the CPU backend runs ggml_cpu_init(), which populates the
     * fp16->fp32 table the q4_K dot needs to read block scales; without it the
     * dot silently returns 0. */
    ggml_backend_t backend = backend_init();
    if (!backend) Rf_error("Rggml_backend_cpu_init failed");

    R_xlen_t nElemA = (R_xlen_t) k * (R_xlen_t) nA;
    R_xlen_t nElemB = (R_xlen_t) k * (R_xlen_t) nB;

    /* Weights -> f32, then quantize to Q4_K into a heap buffer standing in for
     * an mmap'd GGUF payload. Column-major A already lays out ggml row r (= A's
     * column r) contiguously, which is exactly ggml_quantize_chunk's per-row
     * expectation, so no transpose. */
    float *af = (float *) R_alloc((size_t) nElemA, sizeof(float));
    double *Ap = REAL(A_sexp);
    for (R_xlen_t i = 0; i < nElemA; i++) af[i] = (float) Ap[i];

    size_t qa_bytes = ggml_row_size(GGML_TYPE_Q4_K, k) * (size_t) nA;
    void  *qa = malloc(qa_bytes > 0 ? qa_bytes : 1);
    float *bf = (float *) malloc(sizeof(float) * (size_t) (nElemB > 0 ? nElemB : 1));
    if (!qa || !bf) {
        free(qa); free(bf);
        backend_free(backend);
        Rf_error("allocation failure preparing the Q4_K payload / activations");
    }
    ggml_quantize_chunk(GGML_TYPE_Q4_K, af, qa, 0, nA, k, NULL);

    double *Bp = REAL(B_sexp);
    for (R_xlen_t i = 0; i < nElemB; i++) bf[i] = (float) Bp[i];

    size_t mem_size = (size_t) 4 * tensor_overhead() + graph_overhead(8) + 8192;
    struct ggml_context *ctx = ctx_create(mem_size, /*no_alloc=*/1);
    if (!ctx) {
        free(qa); free(bf);
        backend_free(backend);
        Rf_error("Rggml_context_create (ggml_init) failed");
    }

    int64_t neA[2] = { k, nA };
    int64_t neB[2] = { k, nB };
    struct ggml_tensor *tA = new_tensor(ctx, GGML_TYPE_Q4_K, 2, neA, qa);
    struct ggml_tensor *tB = new_tensor(ctx, GGML_TYPE_F32,  2, neB, bf);
    if (!tA || !tB) {
        ctx_free(ctx);
        free(qa); free(bf);
        backend_free(backend);
        Rf_error("Rggml_new_tensor failed");
    }

    SEXP result = PROTECT(Rf_allocMatrix(REALSXP, (int) nA, (int) nB));
    int rc = compute_mul_mat(ctx, backend, tA, tB, NULL, REAL(result));

    ctx_free(ctx);
    free(qa);
    free(bf);
    backend_free(backend);

    if (rc != 0) {
        UNPROTECT(1);
        Rf_error("Rggml_compute_mul_mat (Q4_K) failed with status %d", rc);
    }

    UNPROTECT(1);
    return result;
}

/*
 * RC_rggml_test_mul_mat_backend(A, B, backend)
 *
 * The backend-agnostic path: build the mul_mat graph in a no_alloc context,
 * let the backend allocate every tensor in one of its own buffers, upload the
 * inputs, compute, download the result. Identical code for CPU (0), BLAS (1)
 * and Vulkan (2) - which is exactly the point: a GPU backend's tensors live in
 * device memory, so the host-pointer shortcut Rggml_compute_mul_mat() takes
 * cannot work there.
 *
 * Returns a numeric matrix, dim (ncol(A), ncol(B)) = crossprod(A, B).
 */
SEXP RC_rggml_test_mul_mat_backend(SEXP A_sexp, SEXP B_sexp, SEXP backend_sexp)
{
    if (TYPEOF(A_sexp) != REALSXP || TYPEOF(B_sexp) != REALSXP) {
        Rf_error("A and B must be numeric matrices");
    }
    SEXP dimA = Rf_getAttrib(A_sexp, R_DimSymbol);
    SEXP dimB = Rf_getAttrib(B_sexp, R_DimSymbol);
    if (Rf_length(dimA) != 2 || Rf_length(dimB) != 2) Rf_error("A and B must be matrices");
    int64_t k = INTEGER(dimA)[0], nA = INTEGER(dimA)[1];
    int64_t kB = INTEGER(dimB)[0], nB = INTEGER(dimB)[1];
    if (k != kB) Rf_error("nrow(A) must equal nrow(B)");
    int which = Rf_asInteger(backend_sexp);

    Rggml_context_create_fun   ctx_create = Rggml_context_create_ptr();
    Rggml_context_free_fun     ctx_free   = Rggml_context_free_ptr();
    Rggml_new_tensor_fun       new_tensor = Rggml_new_tensor_ptr();
    Rggml_mul_mat_fun          mul_mat    = Rggml_mul_mat_ptr();
    Rggml_new_graph_fun        new_graph  = Rggml_new_graph_ptr();
    Rggml_build_forward_expand_fun expand = Rggml_build_forward_expand_ptr();
    Rggml_backend_graph_compute_fun compute = Rggml_backend_graph_compute_ptr();
    Rggml_backend_free_fun     bfree      = Rggml_backend_free_ptr();
    Rggml_tensor_overhead_fun  t_over     = Rggml_tensor_overhead_ptr();
    Rggml_graph_overhead_fun   g_over     = Rggml_graph_overhead_ptr();
    Rggml_backend_alloc_ctx_tensors_fun alloc_tensors = Rggml_backend_alloc_ctx_tensors_ptr();
    Rggml_backend_buffer_free_fun buf_free = Rggml_backend_buffer_free_ptr();
    Rggml_backend_tensor_set_fun  t_set    = Rggml_backend_tensor_set_ptr();
    Rggml_backend_tensor_get_fun  t_get    = Rggml_backend_tensor_get_ptr();

    ggml_backend_t backend = NULL;
    switch (which) {
    case 0: backend = Rggml_backend_cpu_init_ptr()(); break;
    case 1: backend = Rggml_backend_blas_init_ptr()(); break;
    case 2: backend = Rggml_backend_vulkan_init_ptr()(0); break;
    default: Rf_error("backend must be 0 (cpu), 1 (blas) or 2 (vulkan)");
    }
    if (!backend) Rf_error("backend %d unavailable (Vulkan needs --with-vulkan and a device)", which);

    size_t mem = (size_t) 8 * t_over() + g_over(8) + 4096;
    struct ggml_context *ctx = ctx_create(mem, /*no_alloc=*/1);
    if (!ctx) { bfree(backend); Rf_error("context creation failed"); }

    int64_t neA[2] = { k, nA }, neB[2] = { k, nB };
    struct ggml_tensor *tA = new_tensor(ctx, GGML_TYPE_F32, 2, neA, NULL);
    struct ggml_tensor *tB = new_tensor(ctx, GGML_TYPE_F32, 2, neB, NULL);
    struct ggml_tensor *tC = (tA && tB) ? mul_mat(ctx, tA, tB) : NULL;
    if (!tC) { ctx_free(ctx); bfree(backend); Rf_error("graph construction failed"); }

    struct ggml_cgraph *gf = new_graph(ctx, 8);
    if (!gf) { ctx_free(ctx); bfree(backend); Rf_error("graph alloc failed"); }
    expand(gf, tC);

    /* one backend buffer holds tA, tB and tC - device memory for a GPU backend */
    ggml_backend_buffer_t buf = alloc_tensors(ctx, backend);
    if (!buf) { ctx_free(ctx); bfree(backend); Rf_error("backend buffer allocation failed"); }

    R_xlen_t nElemA = (R_xlen_t) k * nA, nElemB = (R_xlen_t) k * nB;
    float *af = (float *) R_alloc((size_t) nElemA, sizeof(float));
    float *bf = (float *) R_alloc((size_t) nElemB, sizeof(float));
    for (R_xlen_t i = 0; i < nElemA; i++) af[i] = (float) REAL(A_sexp)[i];
    for (R_xlen_t i = 0; i < nElemB; i++) bf[i] = (float) REAL(B_sexp)[i];
    t_set(tA, af, 0, (size_t) nElemA * sizeof(float));
    t_set(tB, bf, 0, (size_t) nElemB * sizeof(float));

    int rc = compute(backend, gf);

    SEXP out = PROTECT(Rf_allocMatrix(REALSXP, (int) nA, (int) nB));
    if (rc == 0) {
        R_xlen_t n = (R_xlen_t) nA * nB;
        float *cf = (float *) R_alloc((size_t) n, sizeof(float));
        t_get(tC, cf, 0, (size_t) n * sizeof(float));
        for (R_xlen_t i = 0; i < n; i++) REAL(out)[i] = (double) cf[i];
    }

    buf_free(buf);
    ctx_free(ctx);
    bfree(backend);
    if (rc != 0) { UNPROTECT(1); Rf_error("graph compute failed with status %d", rc); }
    UNPROTECT(1);
    return out;
}

/* RC_rggml_vulkan_info() -> list(n_devices, description of device 0 or NA) */
SEXP RC_rggml_vulkan_info(void)
{
    int n = Rggml_backend_vulkan_device_count_ptr()();
    SEXP out = PROTECT(Rf_allocVector(VECSXP, 2));
    SET_VECTOR_ELT(out, 0, Rf_ScalarInteger(n));
    if (n > 0) {
        char buf[256];
        buf[0] = '\0';
        if (Rggml_backend_vulkan_device_description_ptr()(0, buf, sizeof(buf)) == 0) {
            SET_VECTOR_ELT(out, 1, Rf_mkString(buf));
        } else {
            SET_VECTOR_ELT(out, 1, Rf_ScalarString(NA_STRING));
        }
    } else {
        SET_VECTOR_ELT(out, 1, Rf_ScalarString(NA_STRING));
    }
    SEXP nm = PROTECT(Rf_allocVector(STRSXP, 2));
    SET_STRING_ELT(nm, 0, Rf_mkChar("n_devices"));
    SET_STRING_ELT(nm, 1, Rf_mkChar("device"));
    Rf_setAttrib(out, R_NamesSymbol, nm);
    UNPROTECT(2);
    return out;
}

/*
 * RC_rggml_cpu_info() -> list(arch_kernels, simd_dispatch, sgemm, vulkan)
 *
 * Reports what configure actually compiled, read straight off the preprocessor
 * symbols it put in PKG_CPPFLAGS. This exists because a build that silently
 * misses its intended branch - GGML_CPU_GENERIC where NEON kernels were meant,
 * the dgemm_ promotion where sgemm_ was meant - still compiles, still passes
 * every numerical test, and is indistinguishable from the intended one. It is
 * cheaper to make the build state observable than to audit disassembly.
 */
SEXP RC_rggml_cpu_info(void)
{
    static const char *const kernels =
#ifdef RGGML_ARCH_ARM
        "arm";
#else
        "generic";
#endif
    const Rboolean dispatch =
#if defined(RGGML_SIMD_DISPATCH) && RGGML_SIMD_DISPATCH
        TRUE;
#else
        FALSE;
#endif
    const Rboolean sgemm =
#ifdef RGGML_HAVE_SGEMM
        TRUE;
#else
        FALSE;
#endif
    const Rboolean vulkan =
#ifdef RGGML_HAVE_VULKAN
        TRUE;
#else
        FALSE;
#endif

    SEXP out = PROTECT(Rf_allocVector(VECSXP, 4));
    SET_VECTOR_ELT(out, 0, Rf_mkString(kernels));
    SET_VECTOR_ELT(out, 1, Rf_ScalarLogical(dispatch));
    SET_VECTOR_ELT(out, 2, Rf_ScalarLogical(sgemm));
    SET_VECTOR_ELT(out, 3, Rf_ScalarLogical(vulkan));

    SEXP nm = PROTECT(Rf_allocVector(STRSXP, 4));
    SET_STRING_ELT(nm, 0, Rf_mkChar("arch_kernels"));
    SET_STRING_ELT(nm, 1, Rf_mkChar("simd_dispatch"));
    SET_STRING_ELT(nm, 2, Rf_mkChar("sgemm"));
    SET_STRING_ELT(nm, 3, Rf_mkChar("vulkan"));
    Rf_setAttrib(out, R_NamesSymbol, nm);
    UNPROTECT(2);
    return out;
}
