/*
 * rllm_backend.c - Rggml as Rfmalloc's codec-aware (typed) matrix-multiply
 * backend, plus a quantizer that turns a dense R matrix into an Rfmalloc-backed
 * quantized tensor payload.
 *
 * Rfmalloc's typed-GEMM hook (Rfmalloc_register_matmul_backend_ex)
 * hands a backend the *raw compressed payload* of a typed tensor together with
 * a dense double operand, so an engine can multiply without Rfmalloc decoding
 * the tensor to f64 first. Because Rgguf registers the GGUF quantized codecs
 * ("q4_k", "q6_k", ...) with block layouts byte-identical to GGML's, an
 * fmalloc payload for one of those codecs *is* a valid GGML tensor's row data:
 * we point a GGML tensor at it zero-copy and let ggml_mul_mat() contract it,
 * routing q4_K rows through Rggml's runtime-SIMD-dispatched vec_dot.
 *
 * Everything crossing into Rggml/Rfmalloc goes through their installed headers'
 * R_GetCCallable() accessors (Rggml.h, Rfmalloc.h); Rllm links against neither
 * shared object.
 */
#include <stdlib.h>
#include <string.h>

#include <R.h>
#include <Rinternals.h>

#include <Rfmalloc.h>
#include <Rggml.h>

/* GGUF/Rfmalloc codec name -> GGML quantized type. Only the types GGML can use
 * as the quantized operand of mul_mat are mapped; anything else is declined so
 * Rfmalloc falls back to its own decode+BLAS path. (Non-static: rllm_graph.c
 * extends it with the float storage types for weight tensors.) */
int rllm_ggml_type_from_codec(const char *codec, enum ggml_type *out)
{
    if (!codec) return -1;
    if      (!strcmp(codec, "q4_0")) *out = GGML_TYPE_Q4_0;
    else if (!strcmp(codec, "q4_1")) *out = GGML_TYPE_Q4_1;
    else if (!strcmp(codec, "q5_0")) *out = GGML_TYPE_Q5_0;
    else if (!strcmp(codec, "q5_1")) *out = GGML_TYPE_Q5_1;
    else if (!strcmp(codec, "q8_0")) *out = GGML_TYPE_Q8_0;
    else if (!strcmp(codec, "q2_k")) *out = GGML_TYPE_Q2_K;
    else if (!strcmp(codec, "q3_k")) *out = GGML_TYPE_Q3_K;
    else if (!strcmp(codec, "q4_k")) *out = GGML_TYPE_Q4_K;
    else if (!strcmp(codec, "q5_k")) *out = GGML_TYPE_Q5_K;
    else if (!strcmp(codec, "q6_k")) *out = GGML_TYPE_Q6_K;
    else return -1;
    return 0;
}

/* One CPU backend for the process. Initializing it also runs ggml_cpu_init(),
 * which fills the fp16->fp32 table every q*_K dot reads block scales through -
 * without it the dots silently return 0. Never freed (process-lifetime
 * singleton). */
static ggml_backend_t g_cpu = NULL;

static ggml_backend_t rllm_cpu_backend(void)
{
    if (!g_cpu) {
        Rggml_backend_cpu_init_fun cpu_init = Rggml_backend_cpu_init_ptr();
        g_cpu = cpu_init ? cpu_init() : NULL;
    }
    return g_cpu;
}

/*
 * The typed-GEMM hook. Semantics fixed by Rfmalloc: C = T x D when
 * typed_on_left, else C = D x T; T is the compressed tensor with dims
 * (tensor_nrow, tensor_ncol) laid out column-major, so T's column j is one
 * contiguous run == GGML row j of the payload (ne[0] = tensor_nrow). Return 0
 * if handled, non-zero to decline (Rfmalloc then decodes and uses dgemm).
 *
 * Only C = D x T (typed_on_right) maps to ggml_mul_mat: it contracts over
 * T's first dim (tensor_nrow = the quantized ne[0]), exactly ggml's quantized
 * contraction axis, and is the inference-relevant orientation (a Linear
 * Y = X W with weight W stored (in x out) is X %*% W). T x D would contract
 * over ne[1] of a quantized tensor, which ggml cannot do cheaply, so it is
 * declined.
 */
static int rllm_typed_gemm(const char *codec,
                           const void *payload, size_t payload_bytes,
                           int tensor_nrow, int tensor_ncol, int typed_on_left,
                           const double *dense, int dense_nrow, int dense_ncol,
                           double *C)
{
    if (typed_on_left) return 1;                 /* wrong contraction axis for quantized T */

    enum ggml_type type;
    if (rllm_ggml_type_from_codec(codec, &type) != 0) return 1;

    int64_t K = tensor_nrow;   /* ne[0], quantized contraction dim */
    int64_t N = tensor_ncol;   /* ne[1], number of weight rows     */
    int64_t M = dense_nrow;    /* rows of D; dense_ncol == K        */
    if (K <= 0 || N <= 0 || M <= 0) return 1;
    if (dense_ncol != (int)K) return 1;

    /* Resolve all C-callables before allocating anything: the accessors R-error
     * if Rggml is not loaded, and we do not want a longjmp to leak buffers. */
    Rggml_row_size_fun         row_size   = Rggml_row_size_ptr();
    Rggml_blck_size_fun        blck_size  = Rggml_blck_size_ptr();
    Rggml_context_create_fun   ctx_create = Rggml_context_create_ptr();
    Rggml_context_free_fun     ctx_free   = Rggml_context_free_ptr();
    Rggml_new_tensor_fun       new_tensor = Rggml_new_tensor_ptr();
    Rggml_tensor_overhead_fun  t_over     = Rggml_tensor_overhead_ptr();
    Rggml_graph_overhead_fun   g_over     = Rggml_graph_overhead_ptr();
    Rggml_compute_mul_mat_fun  mmul       = Rggml_compute_mul_mat_ptr();

    if (K % blck_size(type) != 0) return 1;                      /* not whole ggml rows */
    if (payload_bytes < row_size(type, K) * (size_t)N) return 1; /* payload too short   */

    ggml_backend_t cpu = rllm_cpu_backend();
    if (!cpu) return 1;

    /* b = t(D): ne = {K, M} f32, bf[k + m*K] = D[m,k] = dense[m + k*M]. */
    float  *bf  = (float *)  malloc(sizeof(float)  * (size_t)K * (size_t)M);
    double *tmp = (double *) malloc(sizeof(double) * (size_t)N * (size_t)M);
    if (!bf || !tmp) { free(bf); free(tmp); return 1; }
    for (int64_t k = 0; k < K; k++) {
        for (int64_t m = 0; m < M; m++) {
            bf[k + m * K] = (float) dense[m + k * M];
        }
    }

    size_t mem = (size_t)4 * t_over() + g_over(8) + 8192;
    struct ggml_context *ctx = ctx_create(mem, /*no_alloc=*/1);
    if (!ctx) { free(bf); free(tmp); return 1; }

    int64_t neA[2] = { K, N };
    int64_t neB[2] = { K, M };
    struct ggml_tensor *a = new_tensor(ctx, type,          2, neA, (void *) payload);
    struct ggml_tensor *b = new_tensor(ctx, GGML_TYPE_F32, 2, neB, bf);
    if (!a || !b) { ctx_free(ctx); free(bf); free(tmp); return 1; }

    /* result ne = {N, M}; tmp[n + m*N] = sum_k T[k,n] * bf[k + m*K] = (D x T)[m,n]. */
    int rc = mmul(ctx, cpu, a, b, NULL, tmp);
    ctx_free(ctx);
    if (rc != 0) { free(bf); free(tmp); return 1; }

    /* C is (M x N) column-major: C[m + n*M] = (D x T)[m,n] = tmp[n + m*N]. */
    for (int64_t n = 0; n < N; n++) {
        for (int64_t m = 0; m < M; m++) {
            C[m + n * M] = tmp[n + m * N];
        }
    }

    free(bf);
    free(tmp);
    return 0;   /* handled */
}

/*
 * RC_rllm_dequantize(payload, dtype, n)
 *
 * Decode `n` elements of a quantized payload (raw vector) to doubles through
 * GGML's own type-traits to_float - the authoritative reference dequantizer.
 * Exists so the tinytest suite can cross-validate the Rgguf/Rfmalloc codec
 * decoders against GGML's on identical bytes: the two implementations must
 * agree to float tolerance, which separates codec bugs from a quantization
 * type's intrinsic (lossy) error.
 */
SEXP RC_rllm_dequantize(SEXP payload, SEXP dtype_sexp, SEXP n_sexp)
{
    if (TYPEOF(payload) != RAWSXP) Rf_error("payload must be a raw vector");
    if (TYPEOF(dtype_sexp) != STRSXP || Rf_length(dtype_sexp) != 1) {
        Rf_error("dtype must be a single string");
    }
    const char *dtype = CHAR(STRING_ELT(dtype_sexp, 0));
    enum ggml_type type;
    if (rllm_ggml_type_from_codec(dtype, &type) != 0) {
        Rf_error("dtype '%s' is not a supported GGML quantized type", dtype);
    }
    R_xlen_t n = (R_xlen_t) Rf_asReal(n_sexp);
    if (n < 1) Rf_error("n must be positive");

    Rggml_row_size_fun   row_size = Rggml_row_size_ptr();
    Rggml_blck_size_fun  blck     = Rggml_blck_size_ptr();
    Rggml_dequantize_fun dequant  = Rggml_dequantize_ptr();
    if (n % blck(type) != 0) {
        Rf_error("n must be a multiple of the '%s' block size (%lld)",
                 dtype, (long long) blck(type));
    }
    if ((double) XLENGTH(payload) < (double) row_size(type, n)) {
        Rf_error("payload too short for %lld '%s' elements", (long long) n, dtype);
    }

    /* fp16 table must be populated before reading block scales. */
    if (!rllm_cpu_backend()) Rf_error("failed to initialize the GGML CPU backend");

    float *buf = (float *) R_alloc((size_t) n, sizeof(float));
    if (dequant(type, RAW(payload), buf, n) != 0) {
        Rf_error("Rggml_dequantize failed for '%s'", dtype);
    }

    SEXP out = PROTECT(Rf_allocVector(REALSXP, n));
    double *op = REAL(out);
    for (R_xlen_t i = 0; i < n; i++) op[i] = (double) buf[i];
    UNPROTECT(1);
    return out;
}

/* Register the ggml typed backend with Rfmalloc (dense fn NULL: quantized
 * only). Called from .onLoad once Rfmalloc/Rggml are loaded. */
SEXP RC_rllm_register_backend(void)
{
    int rc = Rfmalloc_register_matmul_backend_ex("ggml", NULL, rllm_typed_gemm);
    return Rf_ScalarInteger(rc);
}

/* Bytes an nrow=k, ncol=n tensor of `dtype` occupies (row_size(type,k) * n),
 * for sizing the Rfmalloc raw vector on the R side. Returned as a double so it
 * survives large payloads. */
SEXP RC_rllm_qtensor_nbytes(SEXP dtype_sexp, SEXP k_sexp, SEXP n_sexp)
{
    if (TYPEOF(dtype_sexp) != STRSXP || Rf_length(dtype_sexp) != 1) {
        Rf_error("dtype must be a single string");
    }
    enum ggml_type type;
    if (rllm_ggml_type_from_codec(CHAR(STRING_ELT(dtype_sexp, 0)), &type) != 0) {
        Rf_error("dtype '%s' is not a GGML quantized type Rllm can encode "
                 "(q4_0, q4_1, q5_0, q5_1, q8_0, q2_k, q3_k, q4_k, q5_k, q6_k)",
                 CHAR(STRING_ELT(dtype_sexp, 0)));
    }
    int64_t k = Rf_asInteger(k_sexp);
    int64_t n = Rf_asInteger(n_sexp);
    double nbytes = (double) Rggml_row_size_ptr()(type, k) * (double) n;
    return Rf_ScalarReal(nbytes);
}

/*
 * RC_rllm_quantize_into(x, dtype, payload)
 *
 * Quantize the dense double matrix `x` (nrow = contraction/quantized dim, which
 * must be a multiple of the codec block size) into `dtype`'s GGUF block format,
 * writing directly into the Rfmalloc raw vector `payload` (created on the R
 * side, which handles runtime resolution). Column-major x already lays out ggml
 * row j (= column j of x) contiguously, matching the quantizer's per-row
 * expectation, so no transpose. Returns NULL.
 */
SEXP RC_rllm_quantize_into(SEXP x, SEXP dtype_sexp, SEXP payload)
{
    if (TYPEOF(x) != REALSXP) Rf_error("x must be a numeric matrix");
    SEXP dim = Rf_getAttrib(x, R_DimSymbol);
    if (Rf_length(dim) != 2) Rf_error("x must be a matrix");
    int64_t k = INTEGER(dim)[0];   /* per-row length / quantized dim */
    int64_t n = INTEGER(dim)[1];   /* number of rows                 */

    if (TYPEOF(dtype_sexp) != STRSXP || Rf_length(dtype_sexp) != 1) {
        Rf_error("dtype must be a single string");
    }
    const char *dtype = CHAR(STRING_ELT(dtype_sexp, 0));
    enum ggml_type type;
    if (rllm_ggml_type_from_codec(dtype, &type) != 0) {
        Rf_error("dtype '%s' is not a GGML quantized type Rllm can encode "
                 "(q4_0, q4_1, q5_0, q5_1, q8_0, q2_k, q3_k, q4_k, q5_k, q6_k)", dtype);
    }
    if (TYPEOF(payload) != RAWSXP) Rf_error("payload must be a raw vector");

    Rggml_row_size_fun  row_size = Rggml_row_size_ptr();
    Rggml_blck_size_fun blck     = Rggml_blck_size_ptr();
    Rggml_quantize_fun  quantize = Rggml_quantize_ptr();
    if (k % blck(type) != 0) {
        Rf_error("nrow(x) = %lld must be a multiple of the '%s' block size (%lld)",
                 (long long) k, dtype, (long long) blck(type));
    }
    if ((double) XLENGTH(payload) < (double) row_size(type, k) * (double) n) {
        Rf_error("payload too short for a %lld x %lld '%s' tensor",
                 (long long) k, (long long) n, dtype);
    }

    /* Populate the fp16 table before quantizing (writes fp16 block scales). */
    if (!rllm_cpu_backend()) Rf_error("failed to initialize the GGML CPU backend");

    float *af = (float *) R_alloc((size_t) k * (size_t) n, sizeof(float));
    double *xp = REAL(x);
    for (R_xlen_t i = 0; i < (R_xlen_t) k * (R_xlen_t) n; i++) af[i] = (float) xp[i];

    quantize(type, af, RAW(payload), n, k);
    return R_NilValue;
}
