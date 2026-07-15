library(tinytest)
library(Rllm)

# Ecosystem consistency guarantee: for every GGUF quantized type the stack
# supports, the Rgguf/Rfmalloc codec decoder and GGML's reference dequantizer
# must produce the same values from the same bytes. Rllm is the natural home
# for this test - it is the only package that links all three: it quantizes
# with GGML (rllm_quantize_tensor), decodes once through the Rfmalloc codec
# registry (fmalloc_tensor_materialize -> Rgguf's GGML-backed codecs) and once
# through GGML's type-traits to_float (RC_rllm_dequantize), and demands
# float-level agreement. Divergence here means a decoder bug, never intrinsic
# quantization error. It also pins registry geometry and panel offsets to the
# same reference implementation used by compute.

rt <- Rfmalloc::open_fmalloc(tempfile(fileext = ".bin"))

set.seed(7L)
k <- 512L; n <- 3L
W <- matrix(rnorm(k * n, sd = 0.4), nrow = k, ncol = n)

# All formats decode through Rgguf's authoritative GGML codecs. The comparison
# is consistent by construction and still exercises registration, block
# geometry, and panel offsets independently of Rllm's direct decode surface.
for (ty in c("q4_0", "q4_1", "q8_0", "q2_0", "q2_k", "q4_k", "q6_k",
             "q5_0", "q5_1", "q3_k", "q5_k")) {
    Wt <- rllm_quantize_tensor(W, ty, runtime = rt)

    dec_codec <- as.vector(Rfmalloc::fmalloc_tensor_materialize(Wt))
    dec_ggml  <- .Call("RC_rllm_dequantize", unclass(Wt), ty, length(W),
                       PACKAGE = "Rllm")

    expect_equal(dec_codec, dec_ggml, tolerance = 1e-6, info = ty)
    expect_true(max(abs(dec_ggml)) > 0, info = ty)   # fp16-table sanity

    # And the decode is a faithful (lossy) image of the original: bounded by
    # the type's intrinsic error with headroom (q2_k is 2-bit, hence ~0.30).
    rel_in <- sqrt(sum((dec_ggml - as.vector(W))^2) / sum(W^2))
    max_rel <- switch(ty, q8_0 = 0.02, q6_k = 0.04, q2_0 = 0.80,
                      q2_k = 0.40, q3_k = 0.25, 0.15)
    expect_true(rel_in < max_rel, info = sprintf("%s rel=%.3f", ty, rel_in))
}

message("codec-vs-GGML consistency tests completed")
