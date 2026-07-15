library(tinytest)
library(Rllm)

# Opt-in execution of Google's EmbeddingGemma 300M Q8_0 GGUF. The fixed token
# ids and reference prefix were produced by the pinned model through current
# upstream llama.cpp with flash attention and tensor repacking disabled. The
# hermetic full-vector oracle lives in test_embedding_gemma_forward.R.

path <- Sys.getenv("RLLM_EMBEDDING_GGUF", "")
if (!nzchar(path) || !file.exists(path)) {
    exit_file("set RLLM_EMBEDDING_GGUF to an EmbeddingGemma GGUF to run")
}

backing <- tempfile(fileext = ".bin")
rt <- Rfmalloc::open_fmalloc(backing, mode = "scratch", size_gb = 0.5)
model <- rllm_gguf_model(path, runtime = rt)
expect_equal(model$arch, "gemma-embedding")
expect_equal(length(model$tensors), 316L)
expect_equal(length(rllm_program(model)$parameters), length(model$tensors))

# "Which city is the capital of France?", including the model's BOS and EOS.
tokens <- c(2L, 24249L, 3207L, 563L, 506L, 5279L, 529L, 7001L, 236881L, 1L)
embedding <- rllm_embed(model, tokens)
reference_prefix <- c(
    -0.1160799, 0.0255180, 0.0947058, 0.0144119,
    0.0647673, 0.0148213, -0.0158405, 0.0171922,
    -0.0094353, -0.0153312, -0.0055171, -0.0115693,
    0.0080156, 0.0188103, 0.0876969, 0.0371809
)

expect_equal(length(embedding), 768L)
expect_true(all(is.finite(embedding)))
expect_equal(sqrt(sum(embedding^2)), 1, tolerance = 1e-7)
expect_true(max(abs(embedding[seq_along(reference_prefix)] -
                    reference_prefix)) < 0.004)

if (Rggml::rggml_has_cuda()) {
    cuda_embedding <- rllm_embed(model, tokens, backend = "cuda")
    cosine <- sum(embedding * cuda_embedding) /
        sqrt(sum(embedding^2) * sum(cuda_embedding^2))
    expect_true(cosine > 0.999)
    expect_true(max(abs(cuda_embedding - embedding)) < 0.005)
}

Rfmalloc::cleanup_fmalloc(rt)
unlink(backing)
message("real EmbeddingGemma test completed: ", basename(path))
