library(tinytest)
library(Rllm)

# This regression pins the GGML graph assembled by rllm_forward()
# for a llama-architecture forward pass (RMSNorm -> QKV -> RoPE -> causal
# attention -> SwiGLU) over GGUF weights held in fmalloc storage, and its
# logits must match a pure-R reference implementation of the same arithmetic.
#
# The model is synthetic and written at test time with Rgguf's own writer
# (f32 tensors + llama.* metadata keys), so the test is hermetic: no binary
# fixture, no download, and the reference reads the *same file* the graph
# runs on - both sides see identical f32-rounded weights, so double-precision
# reference vs f32 GGML compute agree to float accumulation error (~1e-4).
# Two configurations: multi-head attention proper, and grouped-query
# attention (n_head_kv < n_head), which exercises GGML's head broadcast.

rt <- Rfmalloc::open_fmalloc(tempfile(fileext = ".bin"))

## ---- pure-R reference ------------------------------------------------------
ref_forward <- function(W, hp, tokens) {
    S <- length(tokens)
    hd <- hp$n_embd / hp$n_head
    n_rep <- hp$n_head / hp$n_head_kv

    rope1 <- function(x, pos) {
        # GGML_ROPE_TYPE_NORMAL: adjacent pairs, theta_j = pos * base^(-2j/nd)
        y <- x
        for (j in seq_len(hp$rope_dims / 2)) {
            th <- pos * hp$rope_base^(-2 * (j - 1) / hp$rope_dims)
            i0 <- 2L * j - 1L; i1 <- 2L * j
            y[i0] <- x[i0] * cos(th) - x[i1] * sin(th)
            y[i1] <- x[i0] * sin(th) + x[i1] * cos(th)
        }
        y
    }
    rmsn <- function(v, w) v / sqrt(mean(v^2) + hp$rms_eps) * w

    X <- W[["token_embd.weight"]][, tokens + 1L, drop = FALSE]  # [n_embd, S]
    for (il in seq_len(hp$n_layer) - 1L) {
        p <- function(s) W[[paste0("blk.", il, ".", s)]]

        cur <- apply(X, 2L, rmsn, w = p("attn_norm.weight"))
        Q <- crossprod(p("attn_q.weight"), cur)
        K <- crossprod(p("attn_k.weight"), cur)
        V <- crossprod(p("attn_v.weight"), cur)
        for (s in seq_len(S)) {
            for (h in seq_len(hp$n_head)) {
                idx <- ((h - 1L) * hd + 1L):(h * hd)
                Q[idx, s] <- rope1(Q[idx, s], s - 1L)
            }
            for (h in seq_len(hp$n_head_kv)) {
                idx <- ((h - 1L) * hd + 1L):(h * hd)
                K[idx, s] <- rope1(K[idx, s], s - 1L)
            }
        }
        O <- matrix(0, hp$n_embd, S)
        for (h in seq_len(hp$n_head)) {
            hk <- (h - 1L) %/% n_rep + 1L
            qi <- ((h - 1L) * hd + 1L):(h * hd)
            ki <- ((hk - 1L) * hd + 1L):(hk * hd)
            for (s in seq_len(S)) {          # causal: keys 1..s
                sc <- colSums(K[ki, seq_len(s), drop = FALSE] * Q[qi, s]) / sqrt(hd)
                pr <- exp(sc - max(sc)); pr <- pr / sum(pr)
                O[qi, s] <- V[ki, seq_len(s), drop = FALSE] %*% pr
            }
        }
        X <- X + crossprod(p("attn_output.weight"), O)

        cur <- apply(X, 2L, rmsn, w = p("ffn_norm.weight"))
        g <- crossprod(p("ffn_gate.weight"), cur)
        u <- crossprod(p("ffn_up.weight"), cur)
        X <- X + crossprod(p("ffn_down.weight"), (g / (1 + exp(-g))) * u)
    }
    Xf <- apply(X, 2L, rmsn, w = W[["output_norm.weight"]])
    out_w <- if ("output.weight" %in% names(W)) W[["output.weight"]] else W[["token_embd.weight"]]
    crossprod(out_w, Xf)   # [n_vocab, S]
}

## ---- synthetic model writer ------------------------------------------------
make_model <- function(path, hp, seed) {
    set.seed(seed)
    hd <- hp$n_embd / hp$n_head
    kv_dim <- hd * hp$n_head_kv
    m <- function(nr, nc, sd = 0.15) matrix(rnorm(nr * nc, sd = sd), nr, nc)
    tensors <- list(
        "token_embd.weight" = m(hp$n_embd, hp$n_vocab),
        "output_norm.weight" = 1 + rnorm(hp$n_embd, sd = 0.05),
        "output.weight" = m(hp$n_embd, hp$n_vocab)
    )
    for (il in seq_len(hp$n_layer) - 1L) {
        pre <- paste0("blk.", il, ".")
        tensors[[paste0(pre, "attn_norm.weight")]] <- 1 + rnorm(hp$n_embd, sd = 0.05)
        tensors[[paste0(pre, "attn_q.weight")]] <- m(hp$n_embd, hp$n_embd)
        tensors[[paste0(pre, "attn_k.weight")]] <- m(hp$n_embd, kv_dim)
        tensors[[paste0(pre, "attn_v.weight")]] <- m(hp$n_embd, kv_dim)
        tensors[[paste0(pre, "attn_output.weight")]] <- m(hp$n_embd, hp$n_embd)
        tensors[[paste0(pre, "ffn_norm.weight")]] <- 1 + rnorm(hp$n_embd, sd = 0.05)
        tensors[[paste0(pre, "ffn_gate.weight")]] <- m(hp$n_embd, hp$n_ff)
        tensors[[paste0(pre, "ffn_up.weight")]] <- m(hp$n_embd, hp$n_ff)
        tensors[[paste0(pre, "ffn_down.weight")]] <- m(hp$n_ff, hp$n_embd)
    }
    Rgguf::gguf_write_tensors(path, tensors, metadata = list(
        "general.architecture" = "llama",
        "llama.block_count" = hp$n_layer,
        "llama.embedding_length" = hp$n_embd,
        "llama.attention.head_count" = hp$n_head,
        "llama.attention.head_count_kv" = hp$n_head_kv,
        "llama.feed_forward_length" = hp$n_ff,
        "llama.attention.layer_norm_rms_epsilon" = hp$rms_eps,
        "llama.rope.freq_base" = hp$rope_base,
        "llama.rope.dimension_count" = hp$rope_dims
    ))
    path
}

## ---- the comparison, MHA and GQA -------------------------------------------
configs <- list(
    mha = list(n_layer = 2, n_embd = 32, n_head = 4, n_head_kv = 4,
               n_ff = 48, n_vocab = 64, rms_eps = 1e-5,
               rope_base = 10000, rope_dims = 8),
    gqa = list(n_layer = 2, n_embd = 32, n_head = 4, n_head_kv = 2,
               n_ff = 48, n_vocab = 64, rms_eps = 1e-5,
               rope_base = 10000, rope_dims = 8)
)

for (cfg_name in names(configs)) {
    hp <- configs[[cfg_name]]
    path <- make_model(tempfile(fileext = ".gguf"), hp, seed = 7L)

    model <- rllm_gguf_model(path, runtime = rt)
    expect_inherits(model, "rllm_model")
    expect_equal(model$hparams$n_layer, hp$n_layer, info = cfg_name)
    expect_equal(model$hparams$n_vocab, hp$n_vocab, info = cfg_name)
    expect_true(all(vapply(model$tensors, function(w) {
        identical(typeof(w$payload), "externalptr") &&
            !Rfmalloc::is_fmalloc_vector(w$payload)
    }, logical(1))), info = paste(cfg_name, "weights borrow GGUF spans"))

    tokens <- c(3L, 41L, 0L, 17L, 63L)          # 0-based ids, S = 5
    logits <- rllm_forward(model, tokens)
    expect_equal(dim(logits), c(hp$n_vocab, length(tokens)), info = cfg_name)
    expect_true(all(is.finite(logits)), info = cfg_name)

    # Reference reads the same f32-rounded weights from the same file.
    W <- Rgguf::gguf_import(path, runtime = rt)
    ref <- ref_forward(W, hp, tokens)

    err <- max(abs(logits - ref)) / max(abs(ref))
    expect_true(err < 1e-4, info = sprintf("%s rel err %.2e", cfg_name, err))

    # And the causal structure is real: last-position logits change when an
    # earlier token changes, first-position logits do not.
    tokens2 <- tokens; tokens2[2L] <- 5L
    logits2 <- rllm_forward(model, tokens2)
    expect_equal(logits2[, 1L], logits[, 1L], info = cfg_name)
    expect_false(isTRUE(all.equal(logits2[, 5L], logits[, 5L])), info = cfg_name)

    unlink(path)
}

message("llama forward-pass tests completed")
