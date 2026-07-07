library(tinytest)
library(Rgguf)

# Regression test: Rgguf's quantized codec decoders (the vendored gguflib
# dequantizers registered with Rfmalloc) must agree with GGML's own reference
# dequantizers on identical bytes. This is what separates a codec bug from a
# quantization type's intrinsic (lossy) error - and it is exactly the test that
# would have caught the upstream gguf-tools Q4_K bug fixed in our vendored
# gguflib.c (the high-nibble half of every 64-weight group was dequantized with
# the previous sub-block's scale/min, making q4_k decodes ~33% wrong).
#
# The fixture (inst/tinytest/fixtures/kquant_ggml_ref.rds) holds, per type, a
# payload quantized by GGML (ggml_quantize_chunk) from set.seed(42) gaussians
# and the f32 values GGML's type-traits to_float decodes it back to. It was
# generated with the Rllm/Rggml stack (see that repo's RC_rllm_dequantize);
# committing payload + expected keeps this test free of any Rggml dependency.

fixture <- readRDS(system.file("tinytest/fixtures/kquant_ggml_ref.rds",
                               package = "Rgguf"))
if (length(fixture) == 0L) exit_file("fixture missing")

rt <- Rfmalloc::open_fmalloc(tempfile(fileext = ".bin"))

for (ty in names(fixture)) {
    f <- fixture[[ty]]

    # Stage the ggml-quantized bytes in fmalloc storage and decode them through
    # Rgguf's registered codec (fmalloc_tensor_materialize).
    payload <- Rfmalloc::create_fmalloc_vector("raw", length = length(f$payload),
                                               runtime = rt,
                                               zero_initialize = FALSE)
    payload[] <- f$payload
    expect_true(Rfmalloc::is_fmalloc_vector(payload), info = ty)

    tt <- Rfmalloc::create_fmalloc_tensor(payload, ty, c(f$n, 1L))
    decoded <- as.vector(Rfmalloc::fmalloc_tensor_materialize(tt))

    # Same bytes, same algorithm: agreement to float round-off, not "close".
    expect_equal(decoded, f$expected, tolerance = 1e-6, info = ty)
    expect_true(max(abs(decoded)) > 0, info = ty)
}

message("codec vs GGML reference decode tests completed")
