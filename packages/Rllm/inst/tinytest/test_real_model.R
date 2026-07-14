library(tinytest)
library(Rllm)

# Opt-in real-model smoke test: set RLLM_TEST_GGUF to the path of a
# llama-architecture GGUF file (e.g. SmolLM2-135M Q4_K_M) to run it. CI and
# CRAN never see this - the hermetic verification lives in
# test_llama_forward.R; this exercises the loader and graph on a real
# quantization mix (q4_k/q5_0/q6_k/q8_0/f32 in typical Q4_K_M files).
#
# Validation record (SmolLM2-135M Q4_K_M, 2026-07-07): with the model's
# decoded weights, the ggml graph matches a pure-R reference forward to
# 9.9e-07 relative (f32-twin roundtrip), and the quantized-path logits agree
# with the reference on the argmax; the ~0.19 relative logit deviation of the
# quantized path is Q8_K activation + quantized-weight arithmetic compounded
# over 30 layers, not graph error.

path <- Sys.getenv("RLLM_TEST_GGUF", "")
if (!nzchar(path) || !file.exists(path)) {
    exit_file("set RLLM_TEST_GGUF to a llama-architecture GGUF to run")
}

rt <- Rfmalloc::open_fmalloc(tempfile(fileext = ".bin"), size_gb = 4)
model <- rllm_gguf_model(path, runtime = rt)
expect_inherits(model, "rllm_model")
hp <- model$hparams
expect_true(hp$n_layer >= 1 && hp$n_vocab > 1000)

tokens <- c(1L, 5L, 9L, 2L)  # arbitrary in-vocab ids: structural checks only
logits <- rllm_forward(model, tokens)
expect_equal(dim(logits), c(hp$n_vocab, length(tokens)))
expect_true(all(is.finite(logits)))
expect_true(sd(logits[, ncol(logits)]) > 0)

# causality on real weights
tokens2 <- tokens; tokens2[2L] <- 7L
logits2 <- rllm_forward(model, tokens2)
expect_equal(logits2[, 1L], logits[, 1L])
expect_false(isTRUE(all.equal(logits2[, length(tokens)], logits[, length(tokens)])))

# KV cache: incremental == full batch on real weights
cache <- rllm_kv_cache(model, n_ctx = 8L, runtime = rt)
pre <- rllm_forward(model, tokens[1:2], cache)
expect_equal(pre, logits[, 1:2], tolerance = 1e-4)
for (k in 3:length(tokens)) {
    step <- rllm_forward(model, tokens[k], cache)
    expect_equal(as.vector(step), logits[, k], tolerance = 1e-4)
}

# bytes in -> bytes out, when the file carries a byte-level BPE tokenizer
# (validation record, SmolLM2-135M Q4_K_M: "The capital of France is Paris.
#  The capital of Germany is" -> " Berlin. The capital of Italy is Rome...";
#  cached CPU decode measured 40.2 tok/s over 128 generated tokens)
if (identical(model$tok_model, "gpt2")) {
    gen <- rllm_generate(model, charToRaw("The capital of France is"), n_new = 4L)
    expect_true(is.raw(gen$raw))
    expect_equal(length(gen$new_ids) + 5L, length(gen$ids))
    expect_equal(rllm_decode(model, rllm_encode(model, charToRaw("hello world"))),
                 charToRaw("hello world"))
}

message("real-model smoke test completed: ", basename(path))
