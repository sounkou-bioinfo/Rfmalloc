library(tinytest)
library(Rllm)

# This model is small enough for a hermetic test but uses Qwen3.5's complete
# four-layer schedule: three gated-delta layers followed by one gated full-
# attention layer. The oracle below implements the published operator
# equations directly in R and reads the same f32-rounded GGUF weights.

runtime_path <- tempfile(fileext = ".bin")
rt <- Rfmalloc::open_fmalloc(runtime_path, mode = "scratch", size_gb = 0.01)

rms_norm <- function(x, gain, eps) {
    scale <- sqrt(colMeans(x^2) + eps)
    sweep(sweep(x, 2L, scale, "/"), 1L, gain, "*")
}

l2_norm <- function(x, eps) x / max(sqrt(sum(x^2)), eps)
silu <- function(x) x / (1 + exp(-x))
sigmoid <- function(x) 1 / (1 + exp(-x))
softplus <- function(x) pmax(x, 0) + log1p(exp(-abs(x)))

rope_neox <- function(x, pos, dims, base) {
    half <- dims %/% 2L
    out <- x
    for (i in seq_len(half)) {
        theta <- pos * base^(-2 * (i - 1L) / dims)
        a <- x[[i]]
        b <- x[[i + half]]
        out[[i]] <- a * cos(theta) - b * sin(theta)
        out[[i + half]] <- a * sin(theta) + b * cos(theta)
    }
    out
}

ref_gated_attention <- function(x, weight, hp) {
    n_token <- ncol(x)
    qg <- crossprod(weight("attn_q.weight"), x)
    key <- crossprod(weight("attn_k.weight"), x)
    value <- crossprod(weight("attn_v.weight"), x)
    query <- matrix(0, hp$inner_dim, n_token)
    gate <- matrix(0, hp$inner_dim, n_token)

    for (token in seq_len(n_token)) {
        for (head in seq_len(hp$n_head)) {
            joint <- (head - 1L) * 2L * hp$head_dim
            at <- (head - 1L) * hp$head_dim + seq_len(hp$head_dim)
            query[at, token] <- rms_norm(
                matrix(qg[joint + seq_len(hp$head_dim), token], ncol = 1L),
                weight("attn_q_norm.weight"), hp$rms_eps
            )[, 1L]
            gate[at, token] <- qg[
                joint + hp$head_dim + seq_len(hp$head_dim), token
            ]
            query[at, token] <- rope_neox(
                query[at, token], token - 1L, hp$rope_dims, hp$rope_base
            )
        }
        for (head in seq_len(hp$n_head_kv)) {
            at <- (head - 1L) * hp$head_dim + seq_len(hp$head_dim)
            key[at, token] <- rms_norm(
                matrix(key[at, token], ncol = 1L),
                weight("attn_k_norm.weight"), hp$rms_eps
            )[, 1L]
            key[at, token] <- rope_neox(
                key[at, token], token - 1L, hp$rope_dims, hp$rope_base
            )
        }
    }

    mixed <- matrix(0, hp$inner_dim, n_token)
    repeats <- hp$n_head %/% hp$n_head_kv
    for (head in seq_len(hp$n_head)) {
        key_head <- (head - 1L) %/% repeats + 1L
        qi <- (head - 1L) * hp$head_dim + seq_len(hp$head_dim)
        ki <- (key_head - 1L) * hp$head_dim + seq_len(hp$head_dim)
        for (token in seq_len(n_token)) {
            score <- colSums(
                key[ki, seq_len(token), drop = FALSE] * query[qi, token]
            ) / sqrt(hp$head_dim)
            probability <- exp(score - max(score))
            probability <- probability / sum(probability)
            mixed[qi, token] <-
                value[ki, seq_len(token), drop = FALSE] %*% probability
        }
    }
    crossprod(weight("attn_output.weight"), mixed * sigmoid(gate))
}

ref_gated_delta <- function(x, weight, hp) {
    n_token <- ncol(x)
    qkv <- crossprod(weight("attn_qkv.weight"), x)
    padded <- cbind(matrix(0, hp$conv_width, hp$conv_kernel - 1L), qkv)
    kernel <- weight("ssm_conv1d.weight")
    mixed <- matrix(0, hp$conv_width, n_token)
    for (token in seq_len(n_token)) {
        window <- padded[, token + seq_len(hp$conv_kernel) - 1L,
                         drop = FALSE]
        mixed[, token] <- silu(rowSums(window * t(kernel)))
    }

    query <- mixed[seq_len(hp$key_width), , drop = FALSE]
    key <- mixed[hp$key_width + seq_len(hp$key_width), , drop = FALSE]
    value <- mixed[2L * hp$key_width + seq_len(hp$inner_dim), , drop = FALSE]
    beta <- sigmoid(crossprod(weight("ssm_beta.weight"), x))
    gate <- softplus(
        sweep(crossprod(weight("ssm_alpha.weight"), x), 1L,
              weight("ssm_dt.bias"), "+")
    )
    gate <- sweep(gate, 1L, weight("ssm_a"), "*")

    state <- array(0, c(hp$state_dim, hp$state_dim, hp$value_heads))
    output <- matrix(0, hp$inner_dim, n_token)
    for (token in seq_len(n_token)) {
        for (head in seq_len(hp$value_heads)) {
            key_head <- (head - 1L) %% hp$key_heads + 1L
            key_at <- (key_head - 1L) * hp$state_dim +
                seq_len(hp$state_dim)
            value_at <- (head - 1L) * hp$state_dim +
                seq_len(hp$state_dim)
            q <- l2_norm(query[key_at, token], hp$rms_eps)
            k <- l2_norm(key[key_at, token], hp$rms_eps)
            v <- value[value_at, token]
            s <- state[, , head] * exp(gate[head, token])
            delta <- (v - drop(crossprod(k, s))) * beta[head, token]
            s <- s + tcrossprod(k, delta)
            state[, , head] <- s
            output[value_at, token] <-
                drop(crossprod(q, s)) / sqrt(hp$state_dim)
        }
    }

    z <- crossprod(weight("attn_gate.weight"), x)
    normalized <- matrix(0, hp$inner_dim, n_token)
    for (token in seq_len(n_token)) {
        for (head in seq_len(hp$value_heads)) {
            at <- (head - 1L) * hp$state_dim + seq_len(hp$state_dim)
            normalized[at, token] <- rms_norm(
                matrix(output[at, token], ncol = 1L),
                weight("ssm_norm.weight"), hp$rms_eps
            )[, 1L]
        }
    }
    crossprod(weight("ssm_out.weight"), normalized * silu(z))
}

ref_forward <- function(W, hp, tokens) {
    x <- W[["token_embd.weight"]][, tokens + 1L, drop = FALSE]
    for (il in seq_len(hp$n_layer) - 1L) {
        prefix <- paste0("blk.", il, ".")
        weight <- function(suffix) W[[paste0(prefix, suffix)]]
        current <- rms_norm(x, weight("attn_norm.weight"), hp$rms_eps)
        operator <- if ((il + 1L) %% hp$full_interval == 0L) {
            ref_gated_attention(current, weight, hp)
        } else {
            ref_gated_delta(current, weight, hp)
        }
        x <- x + operator
        current <- rms_norm(
            x, weight("post_attention_norm.weight"), hp$rms_eps
        )
        gate <- crossprod(weight("ffn_gate.weight"), current)
        up <- crossprod(weight("ffn_up.weight"), current)
        x <- x + crossprod(weight("ffn_down.weight"), silu(gate) * up)
    }
    x <- rms_norm(x, W[["output_norm.weight"]], hp$rms_eps)
    crossprod(W[["output.weight"]], x)
}

make_model <- function(path, hp) {
    set.seed(351L)
    matrix_weight <- function(n_in, n_out, sd = 0.08) {
        matrix(rnorm(n_in * n_out, sd = sd), n_in, n_out)
    }
    tensors <- list(
        "token_embd.weight" = matrix_weight(hp$n_embd, hp$n_vocab),
        "output_norm.weight" = 1 + rnorm(hp$n_embd, sd = 0.04),
        "output.weight" = matrix_weight(hp$n_embd, hp$n_vocab)
    )
    for (il in seq_len(hp$n_layer) - 1L) {
        prefix <- paste0("blk.", il, ".")
        put <- function(suffix, value) {
            tensors[[paste0(prefix, suffix)]] <<- value
        }
        put("attn_norm.weight", 1 + rnorm(hp$n_embd, sd = 0.04))
        put("post_attention_norm.weight",
            1 + rnorm(hp$n_embd, sd = 0.04))
        put("ffn_gate.weight", matrix_weight(hp$n_embd, hp$n_ff))
        put("ffn_up.weight", matrix_weight(hp$n_embd, hp$n_ff))
        put("ffn_down.weight", matrix_weight(hp$n_ff, hp$n_embd))
        if ((il + 1L) %% hp$full_interval == 0L) {
            put("attn_q.weight",
                matrix_weight(hp$n_embd, 2L * hp$inner_dim))
            put("attn_k.weight",
                matrix_weight(hp$n_embd, hp$n_head_kv * hp$head_dim))
            put("attn_v.weight",
                matrix_weight(hp$n_embd, hp$n_head_kv * hp$head_dim))
            put("attn_output.weight",
                matrix_weight(hp$inner_dim, hp$n_embd))
            put("attn_q_norm.weight",
                1 + rnorm(hp$head_dim, sd = 0.04))
            put("attn_k_norm.weight",
                1 + rnorm(hp$head_dim, sd = 0.04))
        } else {
            put("attn_qkv.weight",
                matrix_weight(hp$n_embd, hp$conv_width))
            put("attn_gate.weight",
                matrix_weight(hp$n_embd, hp$inner_dim))
            put("ssm_conv1d.weight",
                matrix_weight(hp$conv_kernel, hp$conv_width, sd = 0.12))
            put("ssm_dt.bias", rnorm(hp$value_heads, sd = 0.08))
            put("ssm_a", -runif(hp$value_heads, 0.1, 0.5))
            put("ssm_beta.weight",
                matrix_weight(hp$n_embd, hp$value_heads))
            put("ssm_alpha.weight",
                matrix_weight(hp$n_embd, hp$value_heads))
            put("ssm_norm.weight",
                1 + rnorm(hp$state_dim, sd = 0.04))
            put("ssm_out.weight",
                matrix_weight(hp$inner_dim, hp$n_embd))
        }
    }

    Rgguf::gguf_write_tensors(path, tensors, metadata = list(
        "general.architecture" = "qwen35",
        "qwen35.block_count" = hp$n_layer,
        "qwen35.context_length" = 128,
        "qwen35.embedding_length" = hp$n_embd,
        "qwen35.feed_forward_length" = hp$n_ff,
        "qwen35.attention.head_count" = hp$n_head,
        "qwen35.attention.head_count_kv" = hp$n_head_kv,
        "qwen35.rope.dimension_sections" = hp$rope_sections,
        "qwen35.rope.freq_base" = hp$rope_base,
        "qwen35.attention.layer_norm_rms_epsilon" = hp$rms_eps,
        "qwen35.attention.key_length" = hp$head_dim,
        "qwen35.attention.value_length" = hp$head_dim,
        "qwen35.ssm.conv_kernel" = hp$conv_kernel,
        "qwen35.ssm.state_size" = hp$state_dim,
        "qwen35.ssm.group_count" = hp$key_heads,
        "qwen35.ssm.time_step_rank" = hp$value_heads,
        "qwen35.ssm.inner_size" = hp$inner_dim,
        "qwen35.full_attention_interval" = hp$full_interval,
        "qwen35.rope.dimension_count" = hp$rope_dims
    ))
    path
}

hp <- list(
    n_layer = 4L, n_embd = 8L, n_head = 2L, n_head_kv = 1L,
    n_ff = 12L, n_vocab = 19L, head_dim = 4L, state_dim = 4L,
    key_heads = 1L, value_heads = 2L, inner_dim = 8L,
    key_width = 4L, conv_width = 16L, conv_kernel = 3L,
    full_interval = 4L, rope_sections = c(1L, 1L, 0L, 0L),
    rope_dims = 4L, rope_base = 10000, rms_eps = 1e-6
)
path <- make_model(tempfile(fileext = ".gguf"), hp)
model <- rllm_gguf_model(path, runtime = rt)
plan <- rllm_plan(model)

expect_equal(plan$architecture, "qwen35")
expect_equal(vapply(plan$layers, function(layer) layer$operator$op,
                    character(1)),
             c(rep("gated_delta_net", 3L), "gated_attention"))
expect_equal(vapply(plan$layers, function(layer) layer$state$op,
                    character(1)),
             c(rep("gated_delta", 3L), "kv"))

tokens <- c(2L, 7L, 1L, 12L, 4L)
logits <- rllm_forward(model, tokens)
expect_equal(dim(logits), c(hp$n_vocab, length(tokens)))
expect_true(all(is.finite(logits)))

W <- Rgguf::gguf_import(path, runtime = rt)
W <- lapply(W, function(x) {
    shape <- dim(x)
    x <- as.numeric(x)
    if (!is.null(shape)) dim(x) <- shape
    x
})
reference <- ref_forward(W, hp, tokens)
relative_error <- max(abs(logits - reference)) / max(abs(reference))
expect_true(relative_error < 3e-4,
            info = sprintf("Qwen3.5 pure-R relative error %.2e",
                           relative_error))

incremental_logits <- function(cache, backend = "cpu") {
    pieces <- list(
        rllm_forward(model, tokens[1:2], cache = cache, backend = backend),
        rllm_forward(model, tokens[3], cache = cache, backend = backend),
        rllm_forward(model, tokens[4:5], cache = cache, backend = backend)
    )
    expect_equal(cache$n_past, length(tokens))
    do.call(cbind, pieces)
}

plain_cache <- rllm_kv_cache(model, n_ctx = 8L)
expect_equal(incremental_logits(plain_cache), logits, tolerance = 4e-5)

backing <- tempfile(fileext = ".bin")
state_runtime <- Rfmalloc::open_fmalloc(backing, mode = "scratch",
                                        size_gb = 0.01)
mapped_cache <- rllm_kv_cache(model, n_ctx = 8L, runtime = state_runtime)
expect_true(any(vapply(mapped_cache$recurrent, Rfmalloc::is_fmalloc_vector,
                       logical(1))))
expect_equal(incremental_logits(mapped_cache), logits, tolerance = 4e-5)

if (Rggml::rggml_has_cuda()) {
    nmse <- function(reference, observed) {
        sum((reference - observed)^2) /
            max(sum(reference^2), .Machine$double.xmin)
    }
    cuda_logits <- rllm_forward(model, tokens, backend = "cuda")
    expect_true(nmse(logits, cuda_logits) < 5e-5,
                info = "Qwen3.5 CUDA whole batch matches CPU")

    cuda_plain <- rllm_kv_cache(model, n_ctx = 8L)
    expect_true(nmse(cuda_logits,
                     incremental_logits(cuda_plain, backend = "cuda")) < 5e-5,
                info = "Qwen3.5 CUDA recurrent and KV state")

    cuda_mapped <- rllm_kv_cache(model, n_ctx = 8L, runtime = state_runtime)
    expect_true(nmse(cuda_logits,
                     incremental_logits(cuda_mapped, backend = "cuda")) < 5e-5,
                info = "Qwen3.5 CUDA fmalloc state")
} else {
    expect_error(rllm_forward(model, tokens, backend = "cuda"),
                 "CUDA backend unavailable")
}
Rfmalloc::cleanup_fmalloc(state_runtime)
Rfmalloc::cleanup_fmalloc(rt)
unlink(c(path, backing, runtime_path))

message("Qwen3.5 gated-attention and gated-delta tests completed")
