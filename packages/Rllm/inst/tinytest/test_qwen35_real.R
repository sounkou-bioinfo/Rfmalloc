library(tinytest)
library(Rllm)

# Opt-in execution of a real Qwen3.5 checkpoint. The ordinary suite already
# pins every equation against a pure-R oracle; this test keeps the multi-GB
# model outside the source package while exercising its real tensor codecs and
# complete heterogeneous schedule when RLLM_QWEN35_GGUF is set.
path <- Sys.getenv("RLLM_QWEN35_GGUF")
if (!nzchar(path) || !file.exists(path)) {
    exit_file("set RLLM_QWEN35_GGUF to a real Qwen3.5 GGUF checkpoint")
}

backing <- tempfile(fileext = ".bin")
rt <- Rfmalloc::open_fmalloc(backing, mode = "scratch", size_gb = 0.1)
model <- rllm_gguf_model(path, runtime = rt)
plan <- rllm_plan(model)

expect_equal(plan$architecture, "qwen35")
operators <- vapply(plan$layers, function(layer) layer$operator$op,
                    character(1))
expect_true(any(operators == "gated_delta_net"))
expect_true(any(operators == "gated_attention"))

logits <- rllm_forward(model, 0L)
expect_equal(dim(logits), c(model$hparams$n_vocab, 1L))
expect_true(all(is.finite(logits)))

# A raw f32 transcript from an upstream portable-CPU build may be supplied for
# a bit-for-bit graph comparison. ISA-specific upstream builds legitimately
# change floating-point accumulation order and are not this reference.
reference_path <- Sys.getenv("RLLM_QWEN35_REFERENCE")
if (nzchar(reference_path) && file.exists(reference_path)) {
    reference <- readBin(reference_path, "numeric", n = length(logits),
                         size = 4L)
    expect_equal(length(reference), length(logits))
    expect_identical(as.numeric(logits), reference)
}

Rfmalloc::cleanup_fmalloc(rt)
unlink(backing)

message("Real Qwen3.5 forward test completed")
