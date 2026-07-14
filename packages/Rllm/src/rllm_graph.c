/*
 * rllm_graph.c - a llama-architecture forward pass assembled from Rggml's
 * C-callable graph ops over borrowed GGUF weight spans. Quantized and float
 * weights point directly into the model's read-only mapping. The graph handles
 * a whole causal batch or appends to an optional KV cache.
 *
 * CPU passes use a no_alloc weight context whose tensors point at R-owned
 * payloads. CUDA creates that context once per model, uploads the encoded
 * weights once, and reuses it; each pass builds a separate transient compute
 * context for inputs, cache mirrors, intermediates and logits.
 */
#include <math.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <R.h>
#include <Rinternals.h>

#include <Rfmalloc.h>
#include <Rggml.h>

/* From rllm_backend.c: codec-name -> quantized ggml type (declines floats). */
int rllm_ggml_type_from_codec(const char *codec, enum ggml_type *out);

/* Weight-tensor types additionally include the float storage formats. */
static int rllm_ggml_type_from_name(const char *name, enum ggml_type *out)
{
    if (!name) return -1;
    if (!strcmp(name, "f32")) { *out = GGML_TYPE_F32; return 0; }
    if (!strcmp(name, "f16")) { *out = GGML_TYPE_F16; return 0; }
    return rllm_ggml_type_from_codec(name, out);
}

static SEXP rllm_list_elt(SEXP list, const char *name)
{
    SEXP names = Rf_getAttrib(list, R_NamesSymbol);
    if (TYPEOF(names) != STRSXP) return R_NilValue;
    for (R_xlen_t i = 0; i < XLENGTH(list); i++) {
        if (!strcmp(CHAR(STRING_ELT(names, i)), name)) {
            return VECTOR_ELT(list, i);
        }
    }
    return R_NilValue;
}

static double rllm_hparam(SEXP hparams, const char *name)
{
    SEXP v = rllm_list_elt(hparams, name);
    if (v == R_NilValue) Rf_error("missing hyperparameter '%s'", name);
    return Rf_asReal(v);
}

struct rllm_upload {
    struct ggml_tensor *tensor;
    const void *data;
    size_t bytes;
};

struct rllm_download {
    const struct ggml_tensor *tensor;
    void *data;
    size_t bytes;
    size_t tensor_offset;
    float *scatter;
    int64_t stride;
    int64_t scatter_offset;
    int64_t rows;
    int64_t cols;
};

struct rllm_cuda_context {
    ggml_backend_t backend;
    struct ggml_context *wctx;
    ggml_backend_buffer_t wbuf;
    struct ggml_tensor **weights;
    R_xlen_t n_weights;
    Rggml_backend_buffer_free_fun buffer_free;
    Rggml_context_free_fun context_free;
    Rggml_backend_free_fun backend_free;
};

static SEXP rllm_cuda_context_tag(void)
{
    return Rf_install("Rllm_cuda_model_context");
}

static void rllm_cuda_context_finalizer(SEXP ext)
{
    struct rllm_cuda_context *ctx =
        (struct rllm_cuda_context *)R_ExternalPtrAddr(ext);

    if (!ctx) return;
    if (ctx->wbuf && ctx->buffer_free) ctx->buffer_free(ctx->wbuf);
    if (ctx->wctx && ctx->context_free) ctx->context_free(ctx->wctx);
    if (ctx->backend && ctx->backend_free) ctx->backend_free(ctx->backend);
    free(ctx->weights);
    free(ctx);
    R_ClearExternalPtr(ext);
}

static struct rllm_cuda_context *rllm_cuda_context_get(SEXP ext,
                                                       SEXP tensors)
{
    if (TYPEOF(ext) != EXTPTRSXP ||
        R_ExternalPtrTag(ext) != rllm_cuda_context_tag()) {
        Rf_error("invalid CUDA model context");
    }
    if (R_ExternalPtrProtected(ext) != tensors) {
        Rf_error("CUDA model context belongs to a different tensor set");
    }
    struct rllm_cuda_context *ctx =
        (struct rllm_cuda_context *)R_ExternalPtrAddr(ext);
    if (!ctx || !ctx->backend || !ctx->wctx || !ctx->wbuf) {
        Rf_error("CUDA model context has been released");
    }
    return ctx;
}

static void rllm_add_upload(struct rllm_upload *uploads, int *n_uploads,
                            int max_uploads, struct ggml_tensor *tensor,
                            const void *data, size_t bytes)
{
    if (!uploads) return;
    if (*n_uploads >= max_uploads) Rf_error("internal upload table overflow");
    uploads[*n_uploads].tensor = tensor;
    uploads[*n_uploads].data = data;
    uploads[*n_uploads].bytes = bytes;
    ++*n_uploads;
}

/*
 * Wrap one named weight from the R side as a ggml tensor in `wctx`.
 * `tensors[[name]]` is list(payload = raw vector (fmalloc-backed or plain),
 * type = codec/type string, dims = integer vector, GGUF dim[0]-first order).
 */
static struct ggml_tensor *rllm_weight(struct ggml_context *wctx, SEXP tensors,
                                       const char *name,
                                       Rggml_new_tensor_fun new_tensor,
                                       Rfmalloc_storage_data_fun storage_data,
                                       struct rllm_upload *uploads,
                                       int *n_uploads, int max_uploads)
{
    SEXP w = rllm_list_elt(tensors, name);
    if (w == R_NilValue) Rf_error("missing weight tensor '%s'", name);

    SEXP payload = rllm_list_elt(w, "payload");
    SEXP type_s  = rllm_list_elt(w, "type");
    SEXP dims_s  = rllm_list_elt(w, "dims");
    if (TYPEOF(type_s) != STRSXP || XLENGTH(type_s) != 1 ||
        STRING_ELT(type_s, 0) == NA_STRING || TYPEOF(dims_s) != INTSXP) {
        Rf_error("weight '%s' must be list(payload, type = chr, dims = int)", name);
    }

    enum ggml_type type;
    if (rllm_ggml_type_from_name(CHAR(STRING_ELT(type_s, 0)), &type) != 0) {
        Rf_error("weight '%s' has unsupported type '%s'", name,
                 CHAR(STRING_ELT(type_s, 0)));
    }

    int n_dims = (int) XLENGTH(dims_s);
    if (n_dims < 1 || n_dims > 2) {
        Rf_error("weight '%s': expected 1 or 2 dims, got %d", name, n_dims);
    }
    for (int i = 0; i < n_dims; ++i) {
        if (INTEGER(dims_s)[i] == NA_INTEGER || INTEGER(dims_s)[i] <= 0) {
            Rf_error("weight '%s': dimensions must be positive", name);
        }
    }
    int64_t ne[2] = { INTEGER(dims_s)[0], n_dims == 2 ? INTEGER(dims_s)[1] : 1 };

    const void *payload_data;
    size_t payload_bytes;
    if (storage_data(payload, &payload_data, &payload_bytes, NULL) != 0) {
        Rf_error("weight '%s': unsupported storage payload", name);
    }
    Rggml_row_size_fun row_size = Rggml_row_size_ptr();
    const size_t one_row = row_size(type, ne[0]);
    if (one_row == 0 || (uint64_t)ne[1] > SIZE_MAX / one_row ||
        payload_bytes < one_row * (size_t)ne[1]) {
        Rf_error("weight '%s': payload too short", name);
    }

    struct ggml_tensor *t = new_tensor(wctx, type, n_dims, ne,
                                       uploads ? NULL : (void *)payload_data);
    if (!t) Rf_error("failed to wrap weight '%s'", name);
    rllm_add_upload(uploads, n_uploads, max_uploads, t, payload_data,
                    one_row * (size_t)ne[1]);
    return t;
}

static struct ggml_tensor *rllm_layer_weight(struct ggml_context *wctx, SEXP tensors,
                                             int il, const char *suffix,
                                             Rggml_new_tensor_fun new_tensor,
                                             Rfmalloc_storage_data_fun storage_data,
                                             struct rllm_upload *uploads,
                                             int *n_uploads, int max_uploads)
{
    char name[128];
    snprintf(name, sizeof(name), "blk.%d.%s", il, suffix);
    return rllm_weight(wctx, tensors, name, new_tensor, storage_data,
                       uploads, n_uploads, max_uploads);
}

static struct ggml_tensor *rllm_cuda_weight(struct rllm_cuda_context *ctx,
                                            SEXP tensors, const char *name)
{
    SEXP names = Rf_getAttrib(tensors, R_NamesSymbol);

    if (TYPEOF(names) != STRSXP || XLENGTH(names) != ctx->n_weights) {
        Rf_error("model tensors must retain their names");
    }
    for (R_xlen_t i = 0; i < ctx->n_weights; i++) {
        if (!strcmp(CHAR(STRING_ELT(names, i)), name)) return ctx->weights[i];
    }
    Rf_error("missing CUDA weight tensor '%s'", name);
    return NULL;
}

static struct ggml_tensor *rllm_cuda_layer_weight(
    struct rllm_cuda_context *ctx, SEXP tensors, int il, const char *suffix)
{
    char name[128];
    snprintf(name, sizeof(name), "blk.%d.%s", il, suffix);
    return rllm_cuda_weight(ctx, tensors, name);
}

/* Upload the immutable, codec-native model weights once. The external
 * pointer protects the R tensor list, which in turn keeps every borrowed GGUF
 * mapping alive for the lifetime of the device copy. */
SEXP RC_rllm_cuda_model_context(SEXP tensors)
{
    if (TYPEOF(tensors) != VECSXP || XLENGTH(tensors) < 1 ||
        XLENGTH(tensors) > INT_MAX) {
        Rf_error("tensors must be a non-empty named list");
    }
    SEXP names = Rf_getAttrib(tensors, R_NamesSymbol);
    if (TYPEOF(names) != STRSXP || XLENGTH(names) != XLENGTH(tensors)) {
        Rf_error("tensors must be a named list");
    }
    struct rllm_cuda_context *ctx =
        (struct rllm_cuda_context *)calloc(1, sizeof(*ctx));
    if (!ctx) Rf_error("CUDA model context allocation failed");

    SEXP ext = PROTECT(R_MakeExternalPtr(ctx, rllm_cuda_context_tag(), tensors));
    R_RegisterCFinalizerEx(ext, rllm_cuda_context_finalizer, FALSE);

    ctx->buffer_free = Rggml_backend_buffer_free_ptr();
    ctx->context_free = Rggml_context_free_ptr();
    ctx->backend_free = Rggml_backend_free_ptr();
    ctx->n_weights = XLENGTH(tensors);
    ctx->weights = (struct ggml_tensor **)calloc(
        (size_t)ctx->n_weights, sizeof(*ctx->weights));
    if (!ctx->weights) Rf_error("CUDA weight table allocation failed");

    Rggml_backend_cuda_init_fun cuda_init = Rggml_backend_cuda_init_ptr();
    Rggml_context_create_fun ctx_create = Rggml_context_create_ptr();
    Rggml_tensor_overhead_fun t_over = Rggml_tensor_overhead_ptr();
    Rggml_new_tensor_fun new_tensor = Rggml_new_tensor_ptr();
    Rggml_backend_alloc_ctx_tensors_fun alloc_tensors =
        Rggml_backend_alloc_ctx_tensors_ptr();
    Rggml_backend_tensor_set_fun tensor_set = Rggml_backend_tensor_set_ptr();
    Rfmalloc_storage_data_fun storage_data = Rfmalloc_storage_data_ptr();

    ctx->backend = cuda_init(0);
    if (!ctx->backend) {
        Rf_error("CUDA backend unavailable: install Rggml with --with-cuda and use a visible NVIDIA device");
    }
    ctx->wctx = ctx_create((size_t)(ctx->n_weights + 8) * t_over() + 4096,
                           /*no_alloc=*/1);
    if (!ctx->wctx) Rf_error("CUDA weights context creation failed");

    struct rllm_upload *uploads = (struct rllm_upload *)R_alloc(
        (size_t)ctx->n_weights, sizeof(*uploads));
    int n_uploads = 0;
    for (R_xlen_t i = 0; i < ctx->n_weights; i++) {
        if (STRING_ELT(names, i) == NA_STRING) {
            Rf_error("tensor names cannot be missing");
        }
        ctx->weights[i] = rllm_weight(
            ctx->wctx, tensors, CHAR(STRING_ELT(names, i)), new_tensor,
            storage_data, uploads, &n_uploads, (int)ctx->n_weights);
    }

    ctx->wbuf = alloc_tensors(ctx->wctx, ctx->backend);
    if (!ctx->wbuf) Rf_error("CUDA weight-buffer allocation failed");
    for (int i = 0; i < n_uploads; i++) {
        tensor_set(uploads[i].tensor, uploads[i].data, 0, uploads[i].bytes);
    }

    UNPROTECT(1);
    return ext;
}

/*
 * RC_rllm_llama_forward(hparams, tensors, tokens, rope_mode, kcache, vcache,
 *                       n_past, backend, backend_context)
 *
 * hparams: named list with n_layer, n_embd, n_head, n_head_kv, n_ff,
 *          rms_eps, rope_base, rope_dims, n_vocab (all numeric).
 * tensors: named list of weights (GGUF names) as described above; output
 *          projection falls back to "token_embd.weight" when there is no
 *          "output.weight" (tied embeddings).
 * tokens:  integer vector of 0-based token ids.
 * rope_mode: 0 (GGML_ROPE_TYPE_NORMAL, llama) or 2 (NEOX).
 * kcache/vcache: NULL for a cache-less whole-batch pass, or per-layer lists
 *          of raw vectors (plain or fmalloc-backed) holding f32 cache slabs,
 *          each of n_ctx * n_embd_gqa floats. Classic llama.cpp layout: K is
 *          appended flat (position-major, [hd x head x pos]); V is stored
 *          transposed ([pos x dim], row stride n_ctx) so the attention's
 *          V^T x KQ product reads it without a runtime transpose. The graph
 *          writes rows n_past..n_past+S and attends over rows 0..n_past+S.
 * n_past:  number of positions already in the cache (0 without a cache).
 * backend: 0 for CPU, 3 for CUDA. CUDA owns both contexts' buffers and all
 *          movement goes through Rggml's backend-neutral transfer API.
 * backend_context: model-owned CUDA context, NULL for CPU.
 *
 * Returns the logits as a double matrix, dim c(n_vocab, n_tokens).
 */
SEXP RC_rllm_llama_forward(SEXP hparams, SEXP tensors, SEXP tokens_sexp,
                           SEXP rope_mode_sexp, SEXP kcache, SEXP vcache,
                           SEXP n_past_sexp, SEXP backend_sexp,
                           SEXP backend_context)
{
    if (TYPEOF(hparams) != VECSXP || TYPEOF(tensors) != VECSXP) {
        Rf_error("hparams and tensors must be named lists");
    }
    if (TYPEOF(tokens_sexp) != INTSXP || XLENGTH(tokens_sexp) < 1 ||
        XLENGTH(tokens_sexp) > INT_MAX) {
        Rf_error("tokens must be a non-empty integer vector of 0-based ids");
    }

    const int n_layer   = (int) rllm_hparam(hparams, "n_layer");
    const int n_embd    = (int) rllm_hparam(hparams, "n_embd");
    const int n_head    = (int) rllm_hparam(hparams, "n_head");
    const int n_head_kv = (int) rllm_hparam(hparams, "n_head_kv");
    const int n_ff      = (int) rllm_hparam(hparams, "n_ff");
    const int n_vocab   = (int) rllm_hparam(hparams, "n_vocab");
    const double rms_eps   = rllm_hparam(hparams, "rms_eps");
    const double rope_base = rllm_hparam(hparams, "rope_base");
    const int rope_dims    = (int) rllm_hparam(hparams, "rope_dims");
    const int rope_mode    = Rf_asInteger(rope_mode_sexp);
    const int backend_code = Rf_asInteger(backend_sexp);
    const int use_device   = backend_code == 3;

    if (backend_code != 0 && backend_code != 3) {
        Rf_error("backend must be 0 (cpu) or 3 (cuda)");
    }

    if (n_layer < 1 || n_embd < 1 || n_head < 1 || n_head_kv < 1 ||
        n_ff < 1 || n_vocab < 1) {
        Rf_error("invalid hyperparameters");
    }
    if (n_embd % n_head != 0 || n_head % n_head_kv != 0) {
        Rf_error("n_embd must divide by n_head, and n_head by n_head_kv");
    }
    const int head_dim   = n_embd / n_head;
    const int n_embd_gqa = head_dim * n_head_kv;
    const int S          = (int) XLENGTH(tokens_sexp);

    /* -- KV cache arguments -------------------------------------------------- */
    const int use_cache = kcache != R_NilValue;
    const int n_past    = Rf_asInteger(n_past_sexp);
    int64_t n_ctx = 0;
    if (n_past == NA_INTEGER || n_past < 0) {
        Rf_error("n_past must be a non-negative integer");
    }
    if (use_cache) {
        if (TYPEOF(kcache) != VECSXP || TYPEOF(vcache) != VECSXP ||
            XLENGTH(kcache) != n_layer || XLENGTH(vcache) != n_layer) {
            Rf_error("kcache/vcache must be lists of one raw vector per layer");
        }
        R_xlen_t slab = XLENGTH(VECTOR_ELT(kcache, 0));
        R_xlen_t row_bytes = (R_xlen_t)n_embd_gqa * (R_xlen_t)sizeof(float);
        if (slab % row_bytes != 0) {
            Rf_error("KV cache byte length is not a whole number of rows");
        }
        n_ctx = slab / row_bytes;
        if (n_ctx < 1 || (int64_t) n_past + S > n_ctx) {
            Rf_error("KV cache too small: n_past %d + %d tokens > n_ctx %lld",
                     n_past, S, (long long) n_ctx);
        }
        for (int il = 0; il < n_layer; il++) {
            if (TYPEOF(VECTOR_ELT(kcache, il)) != RAWSXP ||
                TYPEOF(VECTOR_ELT(vcache, il)) != RAWSXP ||
                XLENGTH(VECTOR_ELT(kcache, il)) != slab ||
                XLENGTH(VECTOR_ELT(vcache, il)) != slab) {
                Rf_error("KV cache layer %d: expected raw vectors of %lld bytes",
                         il, (long long) slab);
            }
        }
    } else if (n_past != 0) {
        Rf_error("n_past must be 0 without a KV cache");
    }
    const int64_t n_kv = n_past + S;   /* positions attended over */

    /* -- resolve every C-callable up front ---------------------------------- */
    Rggml_context_create_fun     ctx_create = Rggml_context_create_ptr();
    Rggml_context_free_fun       ctx_free   = Rggml_context_free_ptr();
    Rggml_new_tensor_fun         new_tensor = Rggml_new_tensor_ptr();
    Rggml_tensor_data_fun        tdata      = Rggml_tensor_data_ptr();
    Rggml_tensor_overhead_fun    t_over     = Rggml_tensor_overhead_ptr();
    Rggml_graph_overhead_fun     g_over     = Rggml_graph_overhead_ptr();
    Rggml_new_graph_fun          new_graph  = Rggml_new_graph_ptr();
    Rggml_build_forward_expand_fun expand   = Rggml_build_forward_expand_ptr();
    Rggml_backend_cpu_init_fun   cpu_init   = Rggml_backend_cpu_init_ptr();
    Rggml_backend_free_fun       bfree      = Rggml_backend_free_ptr();
    Rggml_backend_graph_compute_fun compute = Rggml_backend_graph_compute_ptr();
    Rggml_backend_alloc_ctx_tensors_fun alloc_tensors =
        Rggml_backend_alloc_ctx_tensors_ptr();
    Rggml_backend_buffer_free_fun buf_free = Rggml_backend_buffer_free_ptr();
    Rggml_backend_tensor_set_fun tensor_set = Rggml_backend_tensor_set_ptr();
    Rggml_backend_tensor_get_fun tensor_get = Rggml_backend_tensor_get_ptr();
    Rggml_mul_mat_fun            mul_mat    = Rggml_mul_mat_ptr();
    Rggml_get_rows_fun           get_rows   = Rggml_get_rows_ptr();
    Rggml_rms_norm_fun           rms_norm   = Rggml_rms_norm_ptr();
    Rggml_mul_fun                mul        = Rggml_mul_ptr();
    Rggml_add_fun                add        = Rggml_add_ptr();
    Rggml_silu_fun               silu       = Rggml_silu_ptr();
    Rggml_scale_fun              scale      = Rggml_scale_ptr();
    Rggml_soft_max_fun           soft_max   = Rggml_soft_max_ptr();
    Rggml_diag_mask_inf_fun      diag_mask  = Rggml_diag_mask_inf_ptr();
    Rggml_rope_fun               rope       = Rggml_rope_ptr();
    Rggml_reshape_2d_fun         reshape_2d = Rggml_reshape_2d_ptr();
    Rggml_reshape_3d_fun         reshape_3d = Rggml_reshape_3d_ptr();
    Rggml_permute_fun            permute    = Rggml_permute_ptr();
    Rggml_cont_fun               cont       = Rggml_cont_ptr();
    Rggml_transpose_fun          transpose  = Rggml_transpose_ptr();
    Rggml_view_1d_fun            view_1d    = Rggml_view_1d_ptr();
    Rggml_view_2d_fun            view_2d    = Rggml_view_2d_ptr();
    Rggml_view_3d_fun            view_3d    = Rggml_view_3d_ptr();
    Rggml_cpy_fun                cpy        = Rggml_cpy_ptr();
    Rfmalloc_storage_data_fun    storage_data = Rfmalloc_storage_data_ptr();

    struct rllm_cuda_context *cuda_ctx = use_device
        ? rllm_cuda_context_get(backend_context, tensors) : NULL;
    ggml_backend_t backend = use_device ? cuda_ctx->backend : cpu_init();
    if (!backend) Rf_error("failed to initialize the GGML CPU backend");

    /* CPU tensors borrow the mapped GGUF bytes directly. CUDA tensors already
     * occupy the model-owned context created on the first device call. */
    const int n_weight_slots = 2 + 2 + n_layer * (9 + 2);
    const int max_uploads = n_weight_slots + 8;
    int n_uploads = 0;
    int n_downloads = 0;
    struct rllm_upload *uploads = use_device
        ? (struct rllm_upload *)R_alloc((size_t)max_uploads, sizeof(*uploads))
        : NULL;
    struct rllm_download *downloads = use_device && use_cache
        ? (struct rllm_download *)R_alloc((size_t)(2 * n_layer), sizeof(*downloads))
        : NULL;
    struct ggml_context *wctx = use_device ? cuda_ctx->wctx :
        ctx_create((size_t)(n_weight_slots + 8) * t_over() + 4096,
                   /*no_alloc=*/1);
    if (!wctx) {
        if (!use_device) bfree(backend);
        Rf_error("weights context creation failed");
    }

    /* -- compute context: structured size estimate, 2x slack ---------------- */
    const size_t graph_sz = (size_t) n_layer * 40 + 16;
    double est =
        (double) n_layer * (4.0 * S * (16.0 * n_embd + 6.0 * n_embd_gqa + 4.0 * n_ff)
                            + 4.0 * 3.5 * (double) n_head * S * (double) n_kv)
        + 4.0 * S * 6.0 * n_embd + 4.0 * 2.5 * (double) n_vocab * S;
    size_t cmem = (size_t)(2.0 * est)
        + ((size_t) n_layer * 40 + 32) * t_over()
        + g_over(graph_sz) + (1u << 20);
    struct ggml_context *cctx = ctx_create(cmem, /*no_alloc=*/use_device ? 1 : 0);
    if (!cctx) {
        if (!use_device) {
            ctx_free(wctx);
            bfree(backend);
        }
        Rf_error("compute context creation failed (%.1f MB)", cmem / 1048576.0);
    }
    ggml_backend_buffer_t cbuf = NULL;

    /* The CUDA model context outlives this call; only transient allocations
     * are released on an error. CPU still owns its backend and weight context. */
#define RLLM_FAIL(...) do { if (cbuf) buf_free(cbuf); ctx_free(cctx); \
                            if (!use_device) { ctx_free(wctx); bfree(backend); } \
                            Rf_error(__VA_ARGS__); } while (0)
#define RLLM_CHECK(t)  do { if (!(t)) RLLM_FAIL("graph construction failed (%s)", #t); } while (0)

    /* -- inputs: token ids and positions ------------------------------------ */
    int64_t neS[1] = { S };
    struct ggml_tensor *tok = new_tensor(cctx, GGML_TYPE_I32, 1, neS, NULL);
    struct ggml_tensor *pos = new_tensor(cctx, GGML_TYPE_I32, 1, neS, NULL);
    RLLM_CHECK(tok && pos);
    {
        int32_t *tp = use_device
            ? (int32_t *)R_alloc((size_t)S, sizeof(*tp))
            : (int32_t *)tdata(tok);
        int32_t *pp = use_device
            ? (int32_t *)R_alloc((size_t)S, sizeof(*pp))
            : (int32_t *)tdata(pos);
        const int *ids = INTEGER(tokens_sexp);
        for (int i = 0; i < S; i++) {
            if (ids[i] < 0 || ids[i] >= n_vocab) {
                RLLM_FAIL("token id %d out of range [0, %d)", ids[i], n_vocab);
            }
            tp[i] = ids[i];
            pp[i] = n_past + i;
        }
        rllm_add_upload(uploads, &n_uploads, max_uploads, tok, tp,
                        (size_t)S * sizeof(*tp));
        rllm_add_upload(uploads, &n_uploads, max_uploads, pos, pp,
                        (size_t)S * sizeof(*pp));
    }

    /* -- weights ------------------------------------------------------------ */
    struct ggml_tensor *tok_embd = use_device
        ? rllm_cuda_weight(cuda_ctx, tensors, "token_embd.weight")
        : rllm_weight(wctx, tensors, "token_embd.weight", new_tensor,
                      storage_data, NULL, &n_uploads, max_uploads);
    struct ggml_tensor *out_norm = use_device
        ? rllm_cuda_weight(cuda_ctx, tensors, "output_norm.weight")
        : rllm_weight(wctx, tensors, "output_norm.weight", new_tensor,
                      storage_data, NULL, &n_uploads, max_uploads);
    struct ggml_tensor *output_w =
        rllm_list_elt(tensors, "output.weight") != R_NilValue
            ? (use_device
                ? rllm_cuda_weight(cuda_ctx, tensors, "output.weight")
                : rllm_weight(wctx, tensors, "output.weight", new_tensor,
                              storage_data, NULL, &n_uploads, max_uploads))
            : tok_embd;   /* tied embeddings */

    /* -- the graph ----------------------------------------------------------- */
    /* Created before the layer loop: with a cache, the per-layer cpy nodes
     * that append K/V must be expanded into the graph ahead of the attention
     * nodes that read the cache views (ggml executes in insertion order). */
    struct ggml_cgraph *gf = new_graph(cctx, graph_sz);
    RLLM_CHECK(gf);
    const size_t fsz = sizeof(float);

    struct ggml_tensor *inpL = get_rows(cctx, tok_embd, tok);   /* [n_embd, S] */
    RLLM_CHECK(inpL);

#define RLLM_LAYER_WEIGHT(suffix) (use_device ? \
    rllm_cuda_layer_weight(cuda_ctx, tensors, il, suffix) : \
    rllm_layer_weight(wctx, tensors, il, suffix, new_tensor, storage_data, \
                      NULL, &n_uploads, max_uploads))

    for (int il = 0; il < n_layer; il++) {
        struct ggml_tensor *attn_norm = RLLM_LAYER_WEIGHT("attn_norm.weight");
        struct ggml_tensor *wq        = RLLM_LAYER_WEIGHT("attn_q.weight");
        struct ggml_tensor *wk        = RLLM_LAYER_WEIGHT("attn_k.weight");
        struct ggml_tensor *wv        = RLLM_LAYER_WEIGHT("attn_v.weight");
        struct ggml_tensor *wo        = RLLM_LAYER_WEIGHT("attn_output.weight");
        struct ggml_tensor *ffn_norm  = RLLM_LAYER_WEIGHT("ffn_norm.weight");
        struct ggml_tensor *w_gate    = RLLM_LAYER_WEIGHT("ffn_gate.weight");
        struct ggml_tensor *w_up      = RLLM_LAYER_WEIGHT("ffn_up.weight");
        struct ggml_tensor *w_down    = RLLM_LAYER_WEIGHT("ffn_down.weight");

        /* attention */
        struct ggml_tensor *cur = mul(cctx, rms_norm(cctx, inpL, rms_eps), attn_norm);
        RLLM_CHECK(cur);

        struct ggml_tensor *Q = mul_mat(cctx, wq, cur);   /* [n_embd, S]     */
        struct ggml_tensor *K = mul_mat(cctx, wk, cur);   /* [n_embd_gqa, S] */
        struct ggml_tensor *V = mul_mat(cctx, wv, cur);   /* [n_embd_gqa, S] */
        RLLM_CHECK(Q && K && V);

        Q = rope(cctx, reshape_3d(cctx, Q, head_dim, n_head, S), pos,
                 rope_dims, rope_mode, rope_base);
        K = rope(cctx, reshape_3d(cctx, K, head_dim, n_head_kv, S), pos,
                 rope_dims, rope_mode, rope_base);
        RLLM_CHECK(Q && K);

        struct ggml_tensor *Qp = permute(cctx, Q, 0, 2, 1, 3);  /* [hd, S, n_head] */
        RLLM_CHECK(Qp);

        struct ggml_tensor *Kall, *Vall;   /* what attention reads */
        if (use_cache) {
            /* CPU borrows the host slabs. CUDA puts transient mirrors in the
             * compute context, separate from the persistent weights. K has the
             * same position-major layout on host and device. The CPU-friendly
             * host V slab is transposed, but the CUDA append kernel only honors
             * a contiguous destination, so its mirror is position-major and is
             * made contiguous in the attention layout after the append. */
            int64_t ne_slab[1] = { n_ctx * n_embd_gqa };
            void *kraw = RAW(VECTOR_ELT(kcache, il));
            void *vraw = RAW(VECTOR_ELT(vcache, il));
            struct ggml_context *cache_ctx = use_device ? cctx : wctx;
            struct ggml_tensor *kc = new_tensor(cache_ctx, GGML_TYPE_F32, 1, ne_slab,
                                                use_device ? NULL : kraw);
            struct ggml_tensor *vc = new_tensor(cache_ctx, GGML_TYPE_F32, 1, ne_slab,
                                                use_device ? NULL : vraw);
            RLLM_CHECK(kc && vc);
            if (use_device) {
                const size_t prefix_elems =
                    (size_t)n_past * (size_t)n_embd_gqa;
                if (prefix_elems > 0) {
                    float *vprefix = (float *)R_alloc(prefix_elems, fsz);
                    const float *vhost = (const float *)vraw;
                    for (int64_t p = 0; p < n_past; p++) {
                        for (int64_t d = 0; d < n_embd_gqa; d++) {
                            vprefix[p * n_embd_gqa + d] =
                                vhost[p + d * n_ctx];
                        }
                    }
                    rllm_add_upload(uploads, &n_uploads, max_uploads, kc,
                                    kraw, prefix_elems * fsz);
                    rllm_add_upload(uploads, &n_uploads, max_uploads, vc,
                                    vprefix, prefix_elems * fsz);
                }
            }

            /* Append K at position n_past. CPU writes V into its transposed
             * host slab. CUDA writes a contiguous position-major V block. */
            struct ggml_tensor *k_dst =
                view_1d(cctx, kc, (int64_t) S * n_embd_gqa,
                        (size_t) n_past * n_embd_gqa * fsz);
            struct ggml_tensor *v_dst = use_device
                ? view_1d(cctx, vc, (int64_t)S * n_embd_gqa,
                          (size_t)n_past * n_embd_gqa * fsz)
                : view_2d(cctx, vc, S, n_embd_gqa, (size_t)n_ctx * fsz,
                          (size_t)n_past * fsz);
            RLLM_CHECK(k_dst && v_dst);
            expand(gf, cpy(cctx, K, k_dst));
            expand(gf, cpy(cctx,
                           use_device ? V :
                               transpose(cctx, reshape_2d(cctx, V, n_embd_gqa, S)),
                           v_dst));

            if (use_device) {
                const size_t block_elems = (size_t)S * (size_t)n_embd_gqa;
                const size_t block_offset =
                    (size_t)n_past * (size_t)n_embd_gqa * fsz;
                float *vblock = (float *)R_alloc(block_elems, fsz);
                downloads[n_downloads++] = (struct rllm_download){
                    kc, (float *)kraw + (size_t)n_past * n_embd_gqa,
                    block_elems * fsz, block_offset, NULL, 0, 0, 0, 0
                };
                downloads[n_downloads++] = (struct rllm_download){
                    vc, vblock, block_elems * fsz, block_offset, (float *)vraw,
                    n_ctx, n_past, S, n_embd_gqa
                };
            }

            /* Attend over everything cached so far (rows 0..n_kv). */
            Kall = view_3d(cctx, kc, head_dim, n_kv, n_head_kv,
                           (size_t) n_embd_gqa * fsz, (size_t) head_dim * fsz, 0);
            if (use_device) {
                struct ggml_tensor *vseq = view_3d(
                    cctx, vc, head_dim, n_head_kv, n_kv,
                    (size_t)head_dim * fsz,
                    (size_t)n_embd_gqa * fsz, 0);
                Vall = cont(cctx, permute(cctx, vseq, 1, 2, 0, 3));
            } else {
                Vall = view_3d(cctx, vc, n_kv, head_dim, n_head_kv,
                               (size_t)n_ctx * fsz,
                               (size_t)n_ctx * head_dim * fsz, 0);
            }
            RLLM_CHECK(Kall && Vall);
        } else {
            Kall = permute(cctx, K, 0, 2, 1, 3);                /* [hd, S, kv] */
            /* V^T per head: [S, hd, n_head_kv], contiguous for the product */
            Vall = cont(cctx, permute(cctx, reshape_3d(cctx, V, head_dim, n_head_kv, S),
                                      1, 2, 0, 3));
            RLLM_CHECK(Kall && Vall);
        }

        /* [n_kv, S, n_head]; ggml broadcasts K's head dim over Q's (GQA) */
        struct ggml_tensor *KQ = mul_mat(cctx, Kall, Qp);
        KQ = scale(cctx, KQ, 1.0 / sqrt((double) head_dim));
        KQ = diag_mask(cctx, KQ, n_past);                       /* causal */
        KQ = soft_max(cctx, KQ);
        RLLM_CHECK(KQ);

        struct ggml_tensor *KQV = mul_mat(cctx, Vall, KQ);      /* [hd, S, n_head] */
        struct ggml_tensor *attn =
            reshape_2d(cctx, cont(cctx, permute(cctx, KQV, 0, 2, 1, 3)),
                       n_embd, S);                              /* [n_embd, S] */
        attn = mul_mat(cctx, wo, attn);
        RLLM_CHECK(attn);

        inpL = add(cctx, inpL, attn);
        RLLM_CHECK(inpL);

        /* SwiGLU feed-forward */
        cur = mul(cctx, rms_norm(cctx, inpL, rms_eps), ffn_norm);
        RLLM_CHECK(cur);
        struct ggml_tensor *gate = silu(cctx, mul_mat(cctx, w_gate, cur));
        struct ggml_tensor *up   = mul_mat(cctx, w_up, cur);
        RLLM_CHECK(gate && up);
        cur = mul_mat(cctx, w_down, mul(cctx, gate, up));
        RLLM_CHECK(cur);

        inpL = add(cctx, inpL, cur);
        RLLM_CHECK(inpL);
    }

    struct ggml_tensor *cur = mul(cctx, rms_norm(cctx, inpL, rms_eps), out_norm);
    struct ggml_tensor *logits = mul_mat(cctx, output_w, cur);  /* [n_vocab, S] */
    RLLM_CHECK(logits);

    expand(gf, logits);

    if (use_device) {
        /* The model weights are already resident. Allocate only this pass's
         * activations, inputs, cache mirror and result, then upload mutable
         * inputs through the backend-neutral transfer interface. */
        cbuf = alloc_tensors(cctx, backend);
        if (!cbuf) RLLM_FAIL("CUDA compute-buffer allocation failed");
        for (int i = 0; i < n_uploads; ++i) {
            tensor_set(uploads[i].tensor, uploads[i].data, 0, uploads[i].bytes);
        }
    }

    if (compute(backend, gf) != 0 /* GGML_STATUS_SUCCESS */) {
        RLLM_FAIL("graph compute failed");
    }

    SEXP out = PROTECT(Rf_allocMatrix(REALSXP, n_vocab, S));
    {
        R_xlen_t n = (R_xlen_t) n_vocab * S;
        const float *lp;
        float *device_logits = NULL;
        if (use_device) {
            device_logits = (float *)R_alloc((size_t)n, sizeof(*device_logits));
            tensor_get(logits, device_logits, 0, (size_t)n * sizeof(*device_logits));
            lp = device_logits;
            for (int i = 0; i < n_downloads; ++i) {
                tensor_get(downloads[i].tensor, downloads[i].data,
                           downloads[i].tensor_offset,
                           downloads[i].bytes);
                if (downloads[i].scatter) {
                    const float *src = (const float *)downloads[i].data;
                    float *dst = downloads[i].scatter;
                    for (int64_t p = 0; p < downloads[i].rows; p++) {
                        for (int64_t d = 0; d < downloads[i].cols; d++) {
                            dst[downloads[i].scatter_offset + p +
                                d * downloads[i].stride] =
                                src[p * downloads[i].cols + d];
                        }
                    }
                }
            }
        } else {
            lp = (const float *)tdata(logits);
        }
        double *op = REAL(out);
        for (R_xlen_t i = 0; i < n; i++) op[i] = (double) lp[i];
    }

    if (cbuf) buf_free(cbuf);
    ctx_free(cctx);
    if (!use_device) {
        ctx_free(wctx);
        bfree(backend);
    }

    UNPROTECT(1);
    return out;

#undef RLLM_CHECK
#undef RLLM_FAIL
#undef RLLM_LAYER_WEIGHT
}
