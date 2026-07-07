library(tinytest)
library(Rllm)

# The sampler is pure logic over a logits vector; test it directly (hermetic,
# no model needed) plus a small integration through rllm_generate on the
# synthetic llama.

samp <- Rllm:::.rllm_sample_next

set.seed(1)
logits <- c(10, 9, 8, 1, 0, -5, -10)   # 0-based ids 0..6, id 0 is the argmax

## temperature 0 is greedy (argmax), regardless of top_k/top_p
expect_equal(samp(logits, 0, 0L, 1), 0L)
expect_equal(samp(logits, 0, 3L, 0.5), 0L)

## top_k = 1 collapses to the argmax even when sampling
for (i in 1:20) expect_equal(samp(logits, 1.0, 1L, 1), 0L)

## sampling only ever returns tokens inside the top_k support
set.seed(42)
draws <- vapply(1:400, function(i) samp(logits, 1.5, 3L, 1), integer(1))
expect_true(all(draws %in% c(0L, 1L, 2L)))       # top-3 ids only
expect_true(length(unique(draws)) > 1)           # genuinely stochastic
expect_true(0L %in% draws && which.max(tabulate(draws + 1L)) - 1L == 0L)  # argmax most frequent

## top_p nucleus: a peaked distribution keeps essentially just the top token
peaked <- c(100, 0, 0, 0)
for (i in 1:20) expect_equal(samp(peaked, 1.0, 0L, 0.9), 0L)

## reproducibility: same seed -> same greedy-vs-sampled stream through generate
rt <- Rfmalloc::open_fmalloc(tempfile(fileext = ".bin"))
hp <- list(n_layer = 2, n_embd = 32, n_head = 4, n_head_kv = 2,
           n_ff = 48, n_vocab = 64, rms_eps = 1e-5,
           rope_base = 10000, rope_dims = 8)
set.seed(3)
hd <- hp$n_embd / hp$n_head; kv <- hd * hp$n_head_kv
m <- function(nr, nc) matrix(rnorm(nr * nc, sd = 0.15), nr, nc)
tensors <- list("token_embd.weight" = m(hp$n_embd, hp$n_vocab),
                "output_norm.weight" = 1 + rnorm(hp$n_embd, sd = 0.05),
                "output.weight" = m(hp$n_embd, hp$n_vocab))
for (il in 0:1) {
    p <- paste0("blk.", il, ".")
    tensors[[paste0(p, "attn_norm.weight")]] <- 1 + rnorm(hp$n_embd, sd = 0.05)
    tensors[[paste0(p, "attn_q.weight")]] <- m(hp$n_embd, hp$n_embd)
    tensors[[paste0(p, "attn_k.weight")]] <- m(hp$n_embd, kv)
    tensors[[paste0(p, "attn_v.weight")]] <- m(hp$n_embd, kv)
    tensors[[paste0(p, "attn_output.weight")]] <- m(hp$n_embd, hp$n_embd)
    tensors[[paste0(p, "ffn_norm.weight")]] <- 1 + rnorm(hp$n_embd, sd = 0.05)
    tensors[[paste0(p, "ffn_gate.weight")]] <- m(hp$n_embd, hp$n_ff)
    tensors[[paste0(p, "ffn_up.weight")]] <- m(hp$n_embd, hp$n_ff)
    tensors[[paste0(p, "ffn_down.weight")]] <- m(hp$n_ff, hp$n_embd)
}
path <- tempfile(fileext = ".gguf")
Rgguf::gguf_write_tensors(path, tensors, metadata = list(
    "general.architecture" = "llama",
    "llama.block_count" = 2, "llama.embedding_length" = 32,
    "llama.attention.head_count" = 4, "llama.attention.head_count_kv" = 2,
    "llama.feed_forward_length" = 48,
    "llama.attention.layer_norm_rms_epsilon" = 1e-5,
    "llama.rope.freq_base" = 10000, "llama.rope.dimension_count" = 8))
model <- rllm_gguf_model(path, runtime = rt)

# greedy is deterministic (seed irrelevant)
g1 <- rllm_generate(model, c(3L, 1L), n_new = 6L)
g2 <- rllm_generate(model, c(3L, 1L), n_new = 6L)
expect_equal(g1$new_ids, g2$new_ids)

# sampling is reproducible under a fixed seed, and (here) differs from greedy
s1 <- rllm_generate(model, c(3L, 1L), n_new = 6L, temperature = 1.2, seed = 7L)
s2 <- rllm_generate(model, c(3L, 1L), n_new = 6L, temperature = 1.2, seed = 7L)
expect_equal(s1$new_ids, s2$new_ids)
expect_true(all(s1$new_ids >= 0L & s1$new_ids < hp$n_vocab))

message("sampling tests completed")
