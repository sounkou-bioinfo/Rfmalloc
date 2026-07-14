library(tinytest)
library(Rllm)

# KV-cache correctness invariant: an incremental pass (prefill + one token at
# a time through rllm_kv_cache) must produce the same logits at every position
# as one cache-less whole-batch pass - same values through a different code
# path (cache views + cpy nodes + n_past-offset RoPE/mask vs the plain graph).
# Checked on the synthetic GQA llama, with both plain-R and fmalloc-backed
# cache slabs (the cache as a disk citizen).

rt <- Rfmalloc::open_fmalloc(tempfile(fileext = ".bin"))

## synthetic GQA llama (same construction as test_llama_forward.R)
hp <- list(n_layer = 2, n_embd = 32, n_head = 4, n_head_kv = 2,
           n_ff = 48, n_vocab = 64, rms_eps = 1e-5,
           rope_base = 10000, rope_dims = 8)
set.seed(11L)
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
path <- tempfile(fileext = ".gguf")
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
model <- rllm_gguf_model(path, runtime = rt)

tokens <- c(3L, 41L, 0L, 17L, 63L, 5L, 29L)   # S = 7
full <- rllm_forward(model, tokens)            # cache-less whole batch

for (backing in c("R", "fmalloc")) {
    cache <- rllm_kv_cache(model, n_ctx = 16L,
                           runtime = if (backing == "fmalloc") rt)
    expect_inherits(cache, "rllm_kv_cache")
    expect_equal(Rfmalloc::is_fmalloc_vector(cache$k[[1L]]),
                 backing == "fmalloc", info = backing)

    # prefill with the first 3 tokens, then decode one token at a time
    pre <- rllm_forward(model, tokens[1:3], cache)
    expect_equal(cache$n_past, 3L, info = backing)
    expect_equal(pre, full[, 1:3], tolerance = 1e-5, info = backing)

    for (k in 4:7) {
        step <- rllm_forward(model, tokens[k], cache)
        expect_equal(as.vector(step), full[, k], tolerance = 1e-5,
                     info = sprintf("%s pos %d", backing, k))
    }
    expect_equal(cache$n_past, 7L, info = backing)

    # overflowing the cache errors cleanly
    expect_error(rllm_forward(model, rep(0L, 10L), cache), "cache too small")
}

if (Rggml::rggml_has_cuda()) {
    nmse <- function(reference, observed) {
        sum((reference - observed)^2) /
            max(sum(reference^2), .Machine$double.xmin)
    }
    full_cuda <- rllm_forward(model, tokens, backend = "cuda")
    expect_true(nmse(full, full_cuda) < 5e-5,
                info = "CUDA whole batch matches CPU")
    resident <- model$.contexts$cuda

    for (backing in c("R", "fmalloc")) {
        cache <- rllm_kv_cache(
            model, n_ctx = 16L,
            runtime = if (backing == "fmalloc") rt
        )
        expect_equal(Rfmalloc::is_fmalloc_vector(cache$k[[1L]]),
                     backing == "fmalloc", info = paste("CUDA", backing))
        pre <- rllm_forward(model, tokens[1:3], cache, backend = "cuda")
        expect_true(nmse(full_cuda[, 1:3], pre) < 5e-5,
                    info = paste("CUDA cache prefill", backing))
        for (k in 4:7) {
            step <- rllm_forward(model, tokens[k], cache, backend = "cuda")
            expect_true(nmse(full_cuda[, k], step[, 1L]) < 5e-5,
                        info = sprintf("CUDA %s cache position %d", backing, k))
        }
        expect_true(identical(model$.contexts$cuda, resident),
                    info = "CUDA weights remain in one model context")
    }

    mixed <- rllm_kv_cache(model, n_ctx = 16L)
    rllm_forward(model, tokens[1:3], mixed, backend = "cpu")
    mixed_step <- rllm_forward(model, tokens[4L], mixed, backend = "cuda")
    expect_true(nmse(full_cuda[, 4L], mixed_step[, 1L]) < 5e-5,
                info = "CPU cache prefix transfers to CUDA")

    mixed <- rllm_kv_cache(model, n_ctx = 16L)
    rllm_forward(model, tokens[1:3], mixed, backend = "cuda")
    mixed_step <- rllm_forward(model, tokens[4L], mixed, backend = "cpu")
    expect_true(nmse(full[, 4L], mixed_step[, 1L]) < 5e-5,
                info = "CUDA cache prefix transfers to CPU")
}

## rllm_generate == manual greedy loop of full re-forwards
prompt <- c(3L, 41L, 0L)
gen <- rllm_generate(model, prompt, n_new = 5L)
ids <- prompt
for (i in 1:5) {
    l <- rllm_forward(model, ids)              # full re-forward each step
    ids <- c(ids, which.max(l[, ncol(l)]) - 1L)
}
expect_equal(gen$ids, ids)
expect_equal(length(gen$new_ids), 5L)
expect_null(gen$raw)                           # synthetic model: no tokenizer

message("KV cache tests completed")
