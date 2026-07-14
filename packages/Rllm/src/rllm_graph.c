/*
 * rllm_graph.c - a llama-architecture forward pass assembled from Rggml's
 * C-callable graph ops over borrowed GGUF weight spans. Quantized and float
 * weights point directly into the model's read-only mapping. The graph handles
 * a whole causal batch or appends to an optional KV cache.
 *
 * Two ggml contexts, llama.cpp-style:
 *   - a no_alloc weights context whose tensors point zero-copy at the R-owned
 *     payloads (valid for the duration of the .Call);
 *   - a compute context sized from the hyperparameters, holding every
 *     intermediate (a plain pool, no allocator reuse - fine for the batch
 *     sizes this entry point targets; gallocr integration is a later step).
 */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
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

/*
 * Wrap one named weight from the R side as a ggml tensor in `wctx`.
 * `tensors[[name]]` is list(payload = raw vector (fmalloc-backed or plain),
 * type = codec/type string, dims = integer vector, GGUF dim[0]-first order).
 */
static struct ggml_tensor *rllm_weight(struct ggml_context *wctx, SEXP tensors,
                                       const char *name,
                                       Rggml_new_tensor_fun new_tensor,
                                       Rfmalloc_storage_data_fun storage_data)
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
                                       (void *)payload_data);
    if (!t) Rf_error("failed to wrap weight '%s'", name);
    return t;
}

static struct ggml_tensor *rllm_layer_weight(struct ggml_context *wctx, SEXP tensors,
                                             int il, const char *suffix,
                                             Rggml_new_tensor_fun new_tensor,
                                             Rfmalloc_storage_data_fun storage_data)
{
    char name[128];
    snprintf(name, sizeof(name), "blk.%d.%s", il, suffix);
    return rllm_weight(wctx, tensors, name, new_tensor, storage_data);
}

/*
 * RC_rllm_llama_forward(hparams, tensors, tokens, rope_mode, kcache, vcache,
 *                       n_past)
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
 *
 * Returns the logits as a double matrix, dim c(n_vocab, n_tokens).
 */
SEXP RC_rllm_llama_forward(SEXP hparams, SEXP tensors, SEXP tokens_sexp,
                           SEXP rope_mode_sexp, SEXP kcache, SEXP vcache,
                           SEXP n_past_sexp)
{
    if (TYPEOF(hparams) != VECSXP || TYPEOF(tensors) != VECSXP) {
        Rf_error("hparams and tensors must be named lists");
    }
    if (TYPEOF(tokens_sexp) != INTSXP || XLENGTH(tokens_sexp) < 1) {
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
    if (use_cache) {
        if (TYPEOF(kcache) != VECSXP || TYPEOF(vcache) != VECSXP ||
            XLENGTH(kcache) != n_layer || XLENGTH(vcache) != n_layer) {
            Rf_error("kcache/vcache must be lists of one raw vector per layer");
        }
        R_xlen_t slab = XLENGTH(VECTOR_ELT(kcache, 0));
        n_ctx = slab / ((R_xlen_t) n_embd_gqa * (R_xlen_t) sizeof(float));
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

    ggml_backend_t backend = cpu_init();
    if (!backend) Rf_error("failed to initialize the GGML CPU backend");

    /* -- weights context: zero-copy wrappers over R-owned payloads (plus the
     * per-layer cache slabs, which are also external R-owned memory) -------- */
    const int n_weight_slots = 2 + 2 + n_layer * (9 + 2);
    struct ggml_context *wctx =
        ctx_create((size_t)(n_weight_slots + 8) * t_over() + 4096, /*no_alloc=*/1);
    if (!wctx) { bfree(backend); Rf_error("weights context creation failed"); }

    /* -- compute context: structured size estimate, 2x slack ---------------- */
    const size_t graph_sz = (size_t) n_layer * 40 + 16;
    double est =
        (double) n_layer * (4.0 * S * (16.0 * n_embd + 6.0 * n_embd_gqa + 4.0 * n_ff)
                            + 4.0 * 3.5 * (double) n_head * S * (double) n_kv)
        + 4.0 * S * 6.0 * n_embd + 4.0 * 2.5 * (double) n_vocab * S;
    size_t cmem = (size_t)(2.0 * est)
        + ((size_t) n_layer * 40 + 32) * t_over()
        + g_over(graph_sz) + (1u << 20);
    struct ggml_context *cctx = ctx_create(cmem, /*no_alloc=*/0);
    if (!cctx) {
        ctx_free(wctx); bfree(backend);
        Rf_error("compute context creation failed (%.1f MB)", cmem / 1048576.0);
    }

    /* From here on, any failure must free both contexts before Rf_error. */
#define RLLM_FAIL(...) do { ctx_free(cctx); ctx_free(wctx); bfree(backend); \
                            Rf_error(__VA_ARGS__); } while (0)
#define RLLM_CHECK(t)  do { if (!(t)) RLLM_FAIL("graph construction failed (%s)", #t); } while (0)

    /* -- inputs: token ids and positions ------------------------------------ */
    int64_t neS[1] = { S };
    struct ggml_tensor *tok = new_tensor(cctx, GGML_TYPE_I32, 1, neS, NULL);
    struct ggml_tensor *pos = new_tensor(cctx, GGML_TYPE_I32, 1, neS, NULL);
    RLLM_CHECK(tok && pos);
    {
        int32_t *tp = (int32_t *) tdata(tok);
        int32_t *pp = (int32_t *) tdata(pos);
        const int *ids = INTEGER(tokens_sexp);
        for (int i = 0; i < S; i++) {
            if (ids[i] < 0 || ids[i] >= n_vocab) {
                RLLM_FAIL("token id %d out of range [0, %d)", ids[i], n_vocab);
            }
            tp[i] = ids[i];
            pp[i] = n_past + i;
        }
    }

    /* -- weights ------------------------------------------------------------ */
    struct ggml_tensor *tok_embd = rllm_weight(
        wctx, tensors, "token_embd.weight", new_tensor, storage_data);
    struct ggml_tensor *out_norm = rllm_weight(
        wctx, tensors, "output_norm.weight", new_tensor, storage_data);
    struct ggml_tensor *output_w =
        rllm_list_elt(tensors, "output.weight") != R_NilValue
            ? rllm_weight(wctx, tensors, "output.weight", new_tensor, storage_data)
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

    for (int il = 0; il < n_layer; il++) {
        struct ggml_tensor *attn_norm = rllm_layer_weight(wctx, tensors, il, "attn_norm.weight", new_tensor, storage_data);
        struct ggml_tensor *wq = rllm_layer_weight(wctx, tensors, il, "attn_q.weight", new_tensor, storage_data);
        struct ggml_tensor *wk = rllm_layer_weight(wctx, tensors, il, "attn_k.weight", new_tensor, storage_data);
        struct ggml_tensor *wv = rllm_layer_weight(wctx, tensors, il, "attn_v.weight", new_tensor, storage_data);
        struct ggml_tensor *wo = rllm_layer_weight(wctx, tensors, il, "attn_output.weight", new_tensor, storage_data);
        struct ggml_tensor *ffn_norm = rllm_layer_weight(wctx, tensors, il, "ffn_norm.weight", new_tensor, storage_data);
        struct ggml_tensor *w_gate = rllm_layer_weight(wctx, tensors, il, "ffn_gate.weight", new_tensor, storage_data);
        struct ggml_tensor *w_up   = rllm_layer_weight(wctx, tensors, il, "ffn_up.weight", new_tensor, storage_data);
        struct ggml_tensor *w_down = rllm_layer_weight(wctx, tensors, il, "ffn_down.weight", new_tensor, storage_data);

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
            /* Wrap this layer's cache slabs (external, R-owned). */
            int64_t ne_slab[1] = { n_ctx * n_embd_gqa };
            struct ggml_tensor *kc = new_tensor(wctx, GGML_TYPE_F32, 1, ne_slab,
                                                RAW(VECTOR_ELT(kcache, il)));
            struct ggml_tensor *vc = new_tensor(wctx, GGML_TYPE_F32, 1, ne_slab,
                                                RAW(VECTOR_ELT(vcache, il)));
            RLLM_CHECK(kc && vc);

            /* Append: K flat at position n_past; V transposed ([pos x dim],
             * row stride n_ctx) so the value product reads it directly. */
            struct ggml_tensor *k_dst =
                view_1d(cctx, kc, (int64_t) S * n_embd_gqa,
                        (size_t) n_past * n_embd_gqa * fsz);
            struct ggml_tensor *v_dst =
                view_2d(cctx, vc, S, n_embd_gqa, (size_t) n_ctx * fsz,
                        (size_t) n_past * fsz);
            RLLM_CHECK(k_dst && v_dst);
            expand(gf, cpy(cctx, K, k_dst));
            expand(gf, cpy(cctx, transpose(cctx, reshape_2d(cctx, V, n_embd_gqa, S)),
                           v_dst));

            /* Attend over everything cached so far (rows 0..n_kv). */
            Kall = view_3d(cctx, kc, head_dim, n_kv, n_head_kv,
                           (size_t) n_embd_gqa * fsz, (size_t) head_dim * fsz, 0);
            Vall = view_3d(cctx, vc, n_kv, head_dim, n_head_kv,
                           (size_t) n_ctx * fsz, (size_t) n_ctx * head_dim * fsz, 0);
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

    if (compute(backend, gf) != 0 /* GGML_STATUS_SUCCESS */) {
        RLLM_FAIL("graph compute failed");
    }

    SEXP out = PROTECT(Rf_allocMatrix(REALSXP, n_vocab, S));
    {
        const float *lp = (const float *) tdata(logits);
        double *op = REAL(out);
        R_xlen_t n = (R_xlen_t) n_vocab * S;
        for (R_xlen_t i = 0; i < n; i++) op[i] = (double) lp[i];
    }

    ctx_free(cctx);
    ctx_free(wctx);
    bfree(backend);

    UNPROTECT(1);
    return out;

#undef RLLM_CHECK
#undef RLLM_FAIL
}
