library(tinytest)
library(Rllm)

# This small EmbeddingGemma graph pins the semantics that a decoder-only
# fixture cannot see: NEOX RoPE, bidirectional and symmetric-window masks,
# query/key normalization, post-branch normalization, GEGLU, mean pooling and
# the two dense embedding projections. The reference imports the exact f32
# weights written to the GGUF file.

rt_path <- tempfile(fileext = ".bin")
rt <- Rfmalloc::open_fmalloc(rt_path, mode = "scratch", size_gb = 0.1)

rms_norm <- function(x, gain, eps) {
    scale <- sqrt(colMeans(x^2) + eps)
    sweep(sweep(x, 2L, scale, "/"), 1L, gain, "*")
}

rope_neox <- function(x, position, base) {
    half <- length(x) %/% 2L
    out <- x
    for (i in seq_len(half)) {
        theta <- position * base^(-2 * (i - 1L) / length(x))
        a <- x[[i]]
        b <- x[[i + half]]
        out[[i]] <- a * cos(theta) - b * sin(theta)
        out[[i + half]] <- a * sin(theta) + b * cos(theta)
    }
    out
}

gelu <- function(x) {
    0.5 * x * (1 + tanh(sqrt(2 / pi) * x * (1 + 0.044715 * x^2)))
}

reference_embedding <- function(W, hp, tokens) {
    n_token <- length(tokens)
    head_dim <- hp$n_embd %/% hp$n_head
    repeats <- hp$n_head %/% hp$n_head_kv
    X <- sqrt(hp$n_embd) *
        W[["token_embd.weight"]][, tokens + 1L, drop = FALSE]

    for (il in seq_len(hp$n_layer) - 1L) {
        prefix <- paste0("blk.", il, ".")
        weight <- function(suffix) W[[paste0(prefix, suffix)]]
        cur <- rms_norm(X, weight("attn_norm.weight"), hp$rms_eps)
        Q <- crossprod(weight("attn_q.weight"), cur)
        K <- crossprod(weight("attn_k.weight"), cur)
        V <- crossprod(weight("attn_v.weight"), cur)

        for (s in seq_len(n_token)) {
            for (h in seq_len(hp$n_head)) {
                at <- (h - 1L) * head_dim + seq_len(head_dim)
                q <- rms_norm(matrix(Q[at, s], ncol = 1L),
                              weight("attn_q_norm.weight"), hp$rms_eps)[, 1L]
                Q[at, s] <- rope_neox(q, s - 1L,
                                      if (il %% hp$sliding_period <
                                          hp$sliding_period - 1L)
                                          hp$rope_base_swa else hp$rope_base)
            }
            for (h in seq_len(hp$n_head_kv)) {
                at <- (h - 1L) * head_dim + seq_len(head_dim)
                k <- rms_norm(matrix(K[at, s], ncol = 1L),
                              weight("attn_k_norm.weight"), hp$rms_eps)[, 1L]
                K[at, s] <- rope_neox(k, s - 1L,
                                      if (il %% hp$sliding_period <
                                          hp$sliding_period - 1L)
                                          hp$rope_base_swa else hp$rope_base)
            }
        }

        O <- matrix(0, hp$n_embd, n_token)
        local <- il %% hp$sliding_period < hp$sliding_period - 1L
        radius <- hp$sliding_window %/% 2L
        for (h in seq_len(hp$n_head)) {
            kh <- (h - 1L) %/% repeats + 1L
            qi <- (h - 1L) * head_dim + seq_len(head_dim)
            ki <- (kh - 1L) * head_dim + seq_len(head_dim)
            for (s in seq_len(n_token)) {
                keys <- if (local) {
                    which(abs(seq_len(n_token) - s) <= radius)
                } else {
                    seq_len(n_token)
                }
                score <- colSums(K[ki, keys, drop = FALSE] * Q[qi, s]) /
                    sqrt(head_dim)
                probability <- exp(score - max(score))
                probability <- probability / sum(probability)
                O[qi, s] <- V[ki, keys, drop = FALSE] %*% probability
            }
        }
        branch <- crossprod(weight("attn_output.weight"), O)
        branch <- rms_norm(branch, weight("post_attention_norm.weight"),
                           hp$rms_eps)
        X <- X + branch

        cur <- rms_norm(X, weight("ffn_norm.weight"), hp$rms_eps)
        gate <- crossprod(weight("ffn_gate.weight"), cur)
        up <- crossprod(weight("ffn_up.weight"), cur)
        branch <- crossprod(weight("ffn_down.weight"), gelu(gate) * up)
        branch <- rms_norm(branch, weight("post_ffw_norm.weight"), hp$rms_eps)
        X <- X + branch
    }

    X <- rms_norm(X, W[["output_norm.weight"]], hp$rms_eps)
    pooled <- rowMeans(X)
    projected <- drop(crossprod(W[["dense_2.weight"]], pooled))
    drop(crossprod(W[["dense_3.weight"]], projected))
}

make_model <- function(path, hp) {
    set.seed(90210L)
    matrix_weight <- function(n_in, n_out, sd = 0.08) {
        matrix(rnorm(n_in * n_out, sd = sd), n_in, n_out)
    }
    tensors <- list(
        "token_embd.weight" = matrix_weight(hp$n_embd, hp$n_vocab),
        "output_norm.weight" = 1 + rnorm(hp$n_embd, sd = 0.04),
        "dense_2.weight" = matrix_weight(hp$n_embd, hp$n_dense),
        "dense_3.weight" = matrix_weight(hp$n_dense, hp$n_embd)
    )
    for (il in seq_len(hp$n_layer) - 1L) {
        prefix <- paste0("blk.", il, ".")
        put <- function(suffix, value) {
            tensors[[paste0(prefix, suffix)]] <<- value
        }
        head_dim <- hp$n_embd %/% hp$n_head
        kv_dim <- head_dim * hp$n_head_kv
        put("attn_norm.weight", 1 + rnorm(hp$n_embd, sd = 0.04))
        put("attn_q.weight", matrix_weight(hp$n_embd, hp$n_embd))
        put("attn_k.weight", matrix_weight(hp$n_embd, kv_dim))
        put("attn_v.weight", matrix_weight(hp$n_embd, kv_dim))
        put("attn_output.weight", matrix_weight(hp$n_embd, hp$n_embd))
        put("attn_q_norm.weight", 1 + rnorm(head_dim, sd = 0.04))
        put("attn_k_norm.weight", 1 + rnorm(head_dim, sd = 0.04))
        put("post_attention_norm.weight",
            1 + rnorm(hp$n_embd, sd = 0.04))
        put("ffn_norm.weight", 1 + rnorm(hp$n_embd, sd = 0.04))
        put("ffn_gate.weight", matrix_weight(hp$n_embd, hp$n_ff))
        put("ffn_up.weight", matrix_weight(hp$n_embd, hp$n_ff))
        put("ffn_down.weight", matrix_weight(hp$n_ff, hp$n_embd))
        put("post_ffw_norm.weight", 1 + rnorm(hp$n_embd, sd = 0.04))
    }

    Rgguf::gguf_write_tensors(path, tensors, metadata = list(
        "general.architecture" = "gemma-embedding",
        "gemma-embedding.block_count" = hp$n_layer,
        "gemma-embedding.embedding_length" = hp$n_embd,
        "gemma-embedding.attention.head_count" = hp$n_head,
        "gemma-embedding.attention.head_count_kv" = hp$n_head_kv,
        "gemma-embedding.attention.key_length" = hp$n_embd %/% hp$n_head,
        "gemma-embedding.attention.value_length" = hp$n_embd %/% hp$n_head,
        "gemma-embedding.feed_forward_length" = hp$n_ff,
        "gemma-embedding.attention.layer_norm_rms_epsilon" = hp$rms_eps,
        "gemma-embedding.rope.freq_base" = hp$rope_base,
        "gemma-embedding.rope.freq_base_swa" = hp$rope_base_swa,
        "gemma-embedding.attention.sliding_window" = hp$sliding_window,
        "gemma-embedding.attention.sliding_window_pattern" = hp$sliding_period,
        "gemma-embedding.pooling_type" = 1L,
        "gemma-embedding.dense_2_feat_in" = hp$n_embd,
        "gemma-embedding.dense_2_feat_out" = hp$n_dense,
        "gemma-embedding.dense_3_feat_in" = hp$n_dense,
        "gemma-embedding.dense_3_feat_out" = hp$n_embd
    ))
    path
}

hp <- list(
    n_layer = 2L, n_embd = 8L, n_head = 2L, n_head_kv = 1L,
    n_ff = 12L, n_vocab = 23L, n_dense = 10L, rms_eps = 1e-6,
    rope_base = 1e6, rope_base_swa = 10000, sliding_window = 2L,
    sliding_period = 2L
)
path <- make_model(tempfile(fileext = ".gguf"), hp)
model <- rllm_gguf_model(path, runtime = rt)
plan <- rllm_plan(model)

expect_equal(plan$architecture, "gemma-embedding")
expect_equal(vapply(plan$layers, function(x) x$operator$rope$mode, integer(1)),
             c(2L, 2L))
expect_equal(vapply(plan$layers, function(x) x$operator$mask$type, character(1)),
             c("symmetric_window", "bidirectional"))
expect_equal(plan$output$pooling, "mean")
program <- rllm_program(model)
expect_true(setequal(names(program$parameters), names(plan$tensors)))
attention_nodes <- Filter(function(node) node$op == "attention", program$nodes)
expect_equal(vapply(attention_nodes, function(node) {
    node$attributes$mask$type
}, character(1)), c("symmetric_window", "bidirectional"))

tokens <- c(2L, 17L, 4L, 11L)
embedding <- rllm_embed(model, tokens, normalize = FALSE)
W <- Rgguf::gguf_import(path, runtime = rt)
W <- lapply(W, function(x) {
    shape <- dim(x)
    x <- as.numeric(x)
    if (!is.null(shape)) dim(x) <- shape
    x
})
reference <- reference_embedding(W, hp, tokens)
relative_error <- max(abs(embedding - reference)) / max(abs(reference))
# GGML's CPU GEGLU uses its fp16 GELU lookup table, while the oracle evaluates
# the same approximation in double precision.
expect_true(relative_error < 5e-4,
            info = sprintf("EmbeddingGemma pure-R relative error %.2e",
                           relative_error))

if (Rggml::rggml_has_cuda()) {
    cuda_embedding <- rllm_embed(model, tokens, normalize = FALSE,
                                 backend = "cuda")
    cuda_nmse <- sum((cuda_embedding - embedding)^2) /
        max(sum(embedding^2), .Machine$double.xmin)
    expect_true(cuda_nmse < 5e-5,
                info = sprintf("EmbeddingGemma CUDA/CPU NMSE %.2e",
                               cuda_nmse))
} else {
    expect_error(rllm_embed(model, tokens, backend = "cuda"),
                 "CUDA backend unavailable")
}

normalized <- rllm_embed(model, tokens)
expect_equal(sqrt(sum(normalized^2)), 1, tolerance = 1e-7)
expect_error(rllm_forward(model, tokens), "use rllm_embed")
expect_error(rllm_kv_cache(model), "no incremental decode state")

Rfmalloc::cleanup_fmalloc(rt)
unlink(c(path, rt_path))
message("EmbeddingGemma semantic-program tests completed")
