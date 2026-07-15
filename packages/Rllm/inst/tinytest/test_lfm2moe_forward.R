library(tinytest)
library(Rllm)

# A small LFM2MoE model exercises the semantic plan without pretending the
# architecture is llama. The pure-R oracle covers NEOX RoPE, per-head Q/K
# normalization, gated causal short convolution, sigmoid top-k routing and
# stacked expert tensors. A second comparison pins mixed KV/convolution state
# across incremental calls.

rt <- Rfmalloc::open_fmalloc(tempfile(fileext = ".bin"))

rms_norm <- function(x, gain, eps) {
    scale <- sqrt(colMeans(x^2) + eps)
    sweep(sweep(x, 2L, scale, "/"), 1L, gain, "*")
}

rope_neox <- function(x, pos, base) {
    half <- length(x) %/% 2L
    out <- x
    for (i in seq_len(half)) {
        theta <- pos * base^(-2 * (i - 1L) / length(x))
        a <- x[[i]]
        b <- x[[i + half]]
        out[[i]] <- a * cos(theta) - b * sin(theta)
        out[[i + half]] <- a * sin(theta) + b * cos(theta)
    }
    out
}

ref_forward <- function(W, hp, tokens) {
    n_token <- length(tokens)
    head_dim <- hp$n_embd %/% hp$n_head
    X <- W[["token_embd.weight"]][, tokens + 1L, drop = FALSE]

    for (il in seq_len(hp$n_layer) - 1L) {
        prefix <- paste0("blk.", il, ".")
        weight <- function(suffix) W[[paste0(prefix, suffix)]]
        cur <- rms_norm(X, weight("attn_norm.weight"), hp$rms_eps)

        n_head_kv <- hp$n_head_kv[[il + 1L]]
        if (n_head_kv == 0L) {
            bcx <- crossprod(weight("shortconv.in_proj.weight"), cur)
            B <- bcx[seq_len(hp$n_embd), , drop = FALSE]
            C <- bcx[hp$n_embd + seq_len(hp$n_embd), , drop = FALSE]
            gate <- bcx[2L * hp$n_embd + seq_len(hp$n_embd), , drop = FALSE]
            bx <- B * gate
            history <- matrix(0, hp$n_embd, hp$l_cache - 1L)
            padded <- cbind(history, bx)
            conv <- matrix(0, hp$n_embd, n_token)
            kernel <- weight("shortconv.conv.weight")
            for (s in seq_len(n_token)) {
                window <- padded[, s + seq_len(hp$l_cache) - 1L, drop = FALSE]
                conv[, s] <- rowSums(window * t(kernel))
            }
            operator_out <- crossprod(
                weight("shortconv.out_proj.weight"), C * conv
            )
        } else {
            kv_dim <- head_dim * n_head_kv
            Q <- crossprod(weight("attn_q.weight"), cur)
            K <- crossprod(weight("attn_k.weight"), cur)
            V <- crossprod(weight("attn_v.weight"), cur)
            q_gain <- weight("attn_q_norm.weight")
            k_gain <- weight("attn_k_norm.weight")
            for (s in seq_len(n_token)) {
                for (h in seq_len(hp$n_head)) {
                    at <- (h - 1L) * head_dim + seq_len(head_dim)
                    q <- rms_norm(matrix(Q[at, s], ncol = 1L), q_gain,
                                  hp$rms_eps)[, 1L]
                    Q[at, s] <- rope_neox(q, s - 1L, hp$rope_base)
                }
                for (h in seq_len(n_head_kv)) {
                    at <- (h - 1L) * head_dim + seq_len(head_dim)
                    k <- rms_norm(matrix(K[at, s], ncol = 1L), k_gain,
                                  hp$rms_eps)[, 1L]
                    K[at, s] <- rope_neox(k, s - 1L, hp$rope_base)
                }
            }
            O <- matrix(0, hp$n_embd, n_token)
            repeats <- hp$n_head %/% n_head_kv
            for (h in seq_len(hp$n_head)) {
                kh <- (h - 1L) %/% repeats + 1L
                qi <- (h - 1L) * head_dim + seq_len(head_dim)
                ki <- (kh - 1L) * head_dim + seq_len(head_dim)
                for (s in seq_len(n_token)) {
                    score <- colSums(
                        K[ki, seq_len(s), drop = FALSE] * Q[qi, s]
                    ) / sqrt(head_dim)
                    prob <- exp(score - max(score))
                    prob <- prob / sum(prob)
                    O[qi, s] <- V[ki, seq_len(s), drop = FALSE] %*% prob
                }
            }
            stopifnot(nrow(V) == kv_dim)
            operator_out <- crossprod(weight("attn_output.weight"), O)
        }
        X <- X + operator_out

        cur <- rms_norm(X, weight("ffn_norm.weight"), hp$rms_eps)
        if (il < hp$n_dense) {
            gate <- crossprod(weight("ffn_gate.weight"), cur)
            up <- crossprod(weight("ffn_up.weight"), cur)
            X <- X + crossprod(
                weight("ffn_down.weight"), gate / (1 + exp(-gate)) * up
            )
        } else {
            ffn <- matrix(0, hp$n_embd, n_token)
            router <- weight("ffn_gate_inp.weight")
            bias <- weight("exp_probs_b.bias")
            gate_w <- weight("ffn_gate_exps.weight")
            up_w <- weight("ffn_up_exps.weight")
            down_w <- weight("ffn_down_exps.weight")
            for (s in seq_len(n_token)) {
                score <- drop(crossprod(router, cur[, s]))
                score <- 1 / (1 + exp(-score))
                selected <- order(score + bias, decreasing = TRUE)[
                    seq_len(hp$n_expert_used)
                ]
                mixture <- score[selected] / sum(score[selected])
                for (i in seq_along(selected)) {
                    expert <- selected[[i]]
                    gate <- drop(crossprod(gate_w[, , expert], cur[, s]))
                    up <- drop(crossprod(up_w[, , expert], cur[, s]))
                    hidden <- gate / (1 + exp(-gate)) * up
                    ffn[, s] <- ffn[, s] + mixture[[i]] * drop(
                        crossprod(down_w[, , expert], hidden)
                    )
                }
            }
            X <- X + ffn
        }
    }

    X <- rms_norm(X, W[["token_embd_norm.weight"]], hp$rms_eps)
    crossprod(W[["token_embd.weight"]], X)
}

make_model <- function(path, hp) {
    set.seed(314L)
    matrix_weight <- function(n_in, n_out, sd = 0.09) {
        matrix(rnorm(n_in * n_out, sd = sd), n_in, n_out)
    }
    tensors <- list(
        "token_embd.weight" = matrix_weight(hp$n_embd, hp$n_vocab),
        "token_embd_norm.weight" = 1 + rnorm(hp$n_embd, sd = 0.04)
    )
    for (il in seq_len(hp$n_layer) - 1L) {
        prefix <- paste0("blk.", il, ".")
        put <- function(suffix, value) {
            tensors[[paste0(prefix, suffix)]] <<- value
        }
        put("attn_norm.weight", 1 + rnorm(hp$n_embd, sd = 0.04))
        put("ffn_norm.weight", 1 + rnorm(hp$n_embd, sd = 0.04))
        n_head_kv <- hp$n_head_kv[[il + 1L]]
        if (n_head_kv == 0L) {
            put("shortconv.in_proj.weight",
                matrix_weight(hp$n_embd, 3L * hp$n_embd))
            put("shortconv.conv.weight",
                matrix_weight(hp$l_cache, hp$n_embd, sd = 0.15))
            put("shortconv.out_proj.weight",
                matrix_weight(hp$n_embd, hp$n_embd))
        } else {
            kv_dim <- hp$n_embd %/% hp$n_head * n_head_kv
            put("attn_q.weight", matrix_weight(hp$n_embd, hp$n_embd))
            put("attn_k.weight", matrix_weight(hp$n_embd, kv_dim))
            put("attn_v.weight", matrix_weight(hp$n_embd, kv_dim))
            put("attn_output.weight", matrix_weight(hp$n_embd, hp$n_embd))
            put("attn_q_norm.weight",
                1 + rnorm(hp$n_embd %/% hp$n_head, sd = 0.04))
            put("attn_k_norm.weight",
                1 + rnorm(hp$n_embd %/% hp$n_head, sd = 0.04))
        }
        if (il < hp$n_dense) {
            put("ffn_gate.weight", matrix_weight(hp$n_embd, hp$n_ff))
            put("ffn_up.weight", matrix_weight(hp$n_embd, hp$n_ff))
            put("ffn_down.weight", matrix_weight(hp$n_ff, hp$n_embd))
        } else {
            expert_array <- function(n_in, n_out) {
                array(rnorm(n_in * n_out * hp$n_expert, sd = 0.09),
                      c(n_in, n_out, hp$n_expert))
            }
            put("ffn_gate_inp.weight",
                matrix_weight(hp$n_embd, hp$n_expert, sd = 0.13))
            put("exp_probs_b.bias", c(0.17, -0.08, 0.05, -0.14))
            put("ffn_gate_exps.weight", expert_array(hp$n_embd, hp$n_ff_expert))
            put("ffn_up_exps.weight", expert_array(hp$n_embd, hp$n_ff_expert))
            put("ffn_down_exps.weight", expert_array(hp$n_ff_expert, hp$n_embd))
        }
    }

    Rgguf::gguf_write_tensors(path, tensors, metadata = list(
        "general.architecture" = "lfm2moe",
        "lfm2moe.block_count" = hp$n_layer,
        "lfm2moe.embedding_length" = hp$n_embd,
        "lfm2moe.attention.head_count" = hp$n_head,
        "lfm2moe.attention.head_count_kv" = hp$n_head_kv,
        "lfm2moe.feed_forward_length" = hp$n_ff,
        "lfm2moe.expert_count" = hp$n_expert,
        "lfm2moe.expert_used_count" = hp$n_expert_used,
        "lfm2moe.expert_feed_forward_length" = hp$n_ff_expert,
        "lfm2moe.leading_dense_block_count" = hp$n_dense,
        "lfm2moe.shortconv.l_cache" = hp$l_cache,
        "lfm2moe.expert_gating_func" = 2,
        "lfm2moe.attention.layer_norm_rms_epsilon" = hp$rms_eps,
        "lfm2moe.rope.freq_base" = hp$rope_base
    ))
    path
}

hp <- list(
    n_layer = 3L, n_embd = 8L, n_head = 2L,
    n_head_kv = c(0L, 1L, 0L), n_ff = 12L, n_vocab = 19L,
    n_expert = 4L, n_expert_used = 2L, n_ff_expert = 6L,
    n_dense = 1L, l_cache = 3L, rms_eps = 1e-5, rope_base = 10000
)
path <- make_model(tempfile(fileext = ".gguf"), hp)
model <- rllm_gguf_model(path, runtime = rt)
plan <- rllm_plan(model)

expect_equal(plan$architecture, "lfm2moe")
expect_equal(vapply(plan$layers, function(x) x$operator$op, character(1)),
             c("shortconv", "attention", "shortconv"))
expect_equal(vapply(plan$layers, function(x) x$feed_forward$op, character(1)),
             c("swiglu", "moe_swiglu", "moe_swiglu"))
expect_equal(vapply(plan$layers, function(x) x$state$op, character(1)),
             c("conv", "kv", "conv"))

tokens <- c(2L, 7L, 1L, 12L)
logits <- rllm_forward(model, tokens)
moe_layer <- which(vapply(plan$layers, function(layer) {
    layer$feed_forward$op == "moe_swiglu"
}, logical(1)))[[1L]]
scaled_model <- model
scaled_model$plan$layers[[moe_layer]]$feed_forward$scale <- 0.5
expect_false(isTRUE(all.equal(rllm_forward(scaled_model, tokens), logits)))
bad_model <- model
bad_model$plan$layers[[moe_layer]]$feed_forward$routing <- "softmax"
expect_error(rllm_forward(bad_model, tokens), "unknown expert routing")

W <- Rgguf::gguf_import(path, runtime = rt)
W <- lapply(W, function(x) {
    shape <- dim(x)
    x <- as.numeric(x)
    if (!is.null(shape)) dim(x) <- shape
    x
})
reference <- ref_forward(W, hp, tokens)
relative_error <- max(abs(logits - reference)) / max(abs(reference))
expect_true(relative_error < 2e-4,
            info = sprintf("LFM2MoE pure-R relative error %.2e", relative_error))

cache <- rllm_kv_cache(model, n_ctx = 8L)
incremental <- do.call(cbind, lapply(tokens, function(token) {
    rllm_forward(model, token, cache = cache)
}))
expect_equal(cache$n_past, length(tokens))
expect_equal(incremental, logits, tolerance = 2e-5)

unlink(path)
message("LFM2MoE semantic-plan tests completed")
