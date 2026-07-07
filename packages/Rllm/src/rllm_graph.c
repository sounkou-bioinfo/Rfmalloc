/*
 * rllm_graph.c - a llama-architecture forward pass assembled from Rggml's
 * C-callable graph ops (API version 5) over weights that live in caller-owned
 * memory: fmalloc-mmap'd GGUF payloads (still quantized, or f32) and small f32
 * buffers for the 1-d norm weights. No KV cache yet: the graph attends over
 * the whole token batch with a causal mask and returns the logits for every
 * position - the "prompt -> logits" milestone, verified against a pure-R
 * reference implementation in the tinytest suite.
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
                                       Rggml_new_tensor_fun new_tensor)
{
    SEXP w = rllm_list_elt(tensors, name);
    if (w == R_NilValue) Rf_error("missing weight tensor '%s'", name);

    SEXP payload = rllm_list_elt(w, "payload");
    SEXP type_s  = rllm_list_elt(w, "type");
    SEXP dims_s  = rllm_list_elt(w, "dims");
    if (TYPEOF(payload) != RAWSXP || TYPEOF(type_s) != STRSXP ||
        TYPEOF(dims_s) != INTSXP) {
        Rf_error("weight '%s' must be list(payload = raw, type = chr, dims = int)", name);
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
    int64_t ne[2] = { INTEGER(dims_s)[0], n_dims == 2 ? INTEGER(dims_s)[1] : 1 };

    Rggml_row_size_fun row_size = Rggml_row_size_ptr();
    if ((double) XLENGTH(payload) < (double) row_size(type, ne[0]) * (double) ne[1]) {
        Rf_error("weight '%s': payload too short", name);
    }

    struct ggml_tensor *t = new_tensor(wctx, type, n_dims, ne, RAW(payload));
    if (!t) Rf_error("failed to wrap weight '%s'", name);
    return t;
}

static struct ggml_tensor *rllm_layer_weight(struct ggml_context *wctx, SEXP tensors,
                                             int il, const char *suffix,
                                             Rggml_new_tensor_fun new_tensor)
{
    char name[128];
    snprintf(name, sizeof(name), "blk.%d.%s", il, suffix);
    return rllm_weight(wctx, tensors, name, new_tensor);
}

/*
 * RC_rllm_llama_forward(hparams, tensors, tokens, rope_mode)
 *
 * hparams: named list with n_layer, n_embd, n_head, n_head_kv, n_ff,
 *          rms_eps, rope_base, rope_dims, n_vocab (all numeric).
 * tensors: named list of weights (GGUF names) as described above; output
 *          projection falls back to "token_embd.weight" when there is no
 *          "output.weight" (tied embeddings).
 * tokens:  integer vector of 0-based token ids.
 * rope_mode: 0 (GGML_ROPE_TYPE_NORMAL, llama) or 2 (NEOX).
 *
 * Returns the logits as a double matrix, dim c(n_vocab, n_tokens).
 */
SEXP RC_rllm_llama_forward(SEXP hparams, SEXP tensors, SEXP tokens_sexp,
                           SEXP rope_mode_sexp)
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

    ggml_backend_t backend = cpu_init();
    if (!backend) Rf_error("failed to initialize the GGML CPU backend");

    /* -- weights context: zero-copy wrappers over R-owned payloads ---------- */
    const int n_weight_slots = 2 + 2 + n_layer * 9;
    struct ggml_context *wctx =
        ctx_create((size_t)(n_weight_slots + 8) * t_over() + 4096, /*no_alloc=*/1);
    if (!wctx) { bfree(backend); Rf_error("weights context creation failed"); }

    /* -- compute context: structured size estimate, 2x slack ---------------- */
    const size_t graph_sz = (size_t) n_layer * 34 + 16;
    {
        /* keep the estimate readable: bytes for f32 intermediates */
    }
    double est =
        (double) n_layer * (4.0 * S * (16.0 * n_embd + 6.0 * n_embd_gqa + 4.0 * n_ff)
                            + 4.0 * 3.5 * (double) n_head * S * S)
        + 4.0 * S * 6.0 * n_embd + 4.0 * 2.5 * (double) n_vocab * S;
    size_t cmem = (size_t)(2.0 * est)
        + ((size_t) n_layer * 34 + 32) * t_over()
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
            pp[i] = i;
        }
    }

    /* -- weights ------------------------------------------------------------ */
    struct ggml_tensor *tok_embd = rllm_weight(wctx, tensors, "token_embd.weight", new_tensor);
    struct ggml_tensor *out_norm = rllm_weight(wctx, tensors, "output_norm.weight", new_tensor);
    struct ggml_tensor *output_w =
        rllm_list_elt(tensors, "output.weight") != R_NilValue
            ? rllm_weight(wctx, tensors, "output.weight", new_tensor)
            : tok_embd;   /* tied embeddings */

    /* -- the graph ----------------------------------------------------------- */
    struct ggml_tensor *inpL = get_rows(cctx, tok_embd, tok);   /* [n_embd, S] */
    RLLM_CHECK(inpL);

    for (int il = 0; il < n_layer; il++) {
        struct ggml_tensor *attn_norm = rllm_layer_weight(wctx, tensors, il, "attn_norm.weight", new_tensor);
        struct ggml_tensor *wq = rllm_layer_weight(wctx, tensors, il, "attn_q.weight", new_tensor);
        struct ggml_tensor *wk = rllm_layer_weight(wctx, tensors, il, "attn_k.weight", new_tensor);
        struct ggml_tensor *wv = rllm_layer_weight(wctx, tensors, il, "attn_v.weight", new_tensor);
        struct ggml_tensor *wo = rllm_layer_weight(wctx, tensors, il, "attn_output.weight", new_tensor);
        struct ggml_tensor *ffn_norm = rllm_layer_weight(wctx, tensors, il, "ffn_norm.weight", new_tensor);
        struct ggml_tensor *w_gate = rllm_layer_weight(wctx, tensors, il, "ffn_gate.weight", new_tensor);
        struct ggml_tensor *w_up   = rllm_layer_weight(wctx, tensors, il, "ffn_up.weight", new_tensor);
        struct ggml_tensor *w_down = rllm_layer_weight(wctx, tensors, il, "ffn_down.weight", new_tensor);

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

        struct ggml_tensor *Qp = permute(cctx, Q, 0, 2, 1, 3);  /* [hd, S, n_head]    */
        struct ggml_tensor *Kp = permute(cctx, K, 0, 2, 1, 3);  /* [hd, S, n_head_kv] */
        RLLM_CHECK(Qp && Kp);

        /* [S, S, n_head]; ggml broadcasts K's head dim over Q's (GQA) */
        struct ggml_tensor *KQ = mul_mat(cctx, Kp, Qp);
        KQ = scale(cctx, KQ, 1.0 / sqrt((double) head_dim));
        KQ = diag_mask(cctx, KQ, /*n_past=*/0);                 /* causal */
        KQ = soft_max(cctx, KQ);
        RLLM_CHECK(KQ);

        /* V^T per head: [S, hd, n_head_kv], contiguous for the row product */
        struct ggml_tensor *Vt =
            cont(cctx, permute(cctx, reshape_3d(cctx, V, head_dim, n_head_kv, S),
                               1, 2, 0, 3));
        RLLM_CHECK(Vt);

        struct ggml_tensor *KQV = mul_mat(cctx, Vt, KQ);        /* [hd, S, n_head] */
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

    struct ggml_cgraph *gf = new_graph(cctx, graph_sz);
    RLLM_CHECK(gf);
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

/*
 * RC_rllm_as_f32(x) - double vector -> raw vector of packed f32 values, for
 * staging 1-d norm weights (loaded as doubles by Rgguf) as ggml F32 buffers.
 */
SEXP RC_rllm_as_f32(SEXP x)
{
    if (TYPEOF(x) != REALSXP) Rf_error("x must be a double vector");
    R_xlen_t n = XLENGTH(x);
    SEXP out = PROTECT(Rf_allocVector(RAWSXP, n * (R_xlen_t) sizeof(float)));
    float *fp = (float *) RAW(out);
    const double *xp = REAL(x);
    for (R_xlen_t i = 0; i < n; i++) fp[i] = (float) xp[i];
    UNPROTECT(1);
    return out;
}
