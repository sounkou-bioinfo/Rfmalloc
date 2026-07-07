library(tinytest)
library(Rggml)

# The quantized weight -> compute path, end to end. A weight matrix is
# quantized to Q4_K into a heap buffer standing in for an mmap'd GGUF payload,
# wrapped zero-copy as a Q4_K tensor, and multiplied by dense F32 activations
# via ggml_mul_mat() on the CPU backend. Each Q4_K weight row is contracted
# against the activation columns through GGML's type-traits vec_dot for Q4_K,
# i.e. the runtime-SIMD-dispatched ggml_vec_dot_q4_K_q8_K (AVX2/NEON where
# staged), with the activations quantized to Q8_K on the fly - exactly as
# llama.cpp does at inference. This is the operation the Rfmalloc typed-GEMM
# bridge will drive on real model weights.
#
# Q4_K + Q8_K are lossy, so the product is NOT bit-exact against the true
# crossprod(A, B); the isolated dot test (test_simd.R) already proves the
# dispatched kernel equals GGML's scalar reference bit-for-bit. Here we assert
# the quantized product tracks the true product tightly (very high correlation,
# small relative error) and honours the same crossprod() convention as the F32
# path (a transpose would collapse the correlation).

message("Testing quantized (Q4_K) mul_mat over an external payload...")

set.seed(1L)

for (dims in list(c(k = 256L, nA = 5L, nB = 3L),
                  c(k = 512L, nA = 8L, nB = 4L))) {
    k  <- dims[["k"]]
    nA <- dims[["nA"]]
    nB <- dims[["nB"]]

    A <- matrix(rnorm(k * nA, sd = 0.5), nrow = k, ncol = nA)  # weights
    B <- matrix(rnorm(k * nB, sd = 1.0), nrow = k, ncol = nB)  # activations

    res <- Rggml:::rggml_test_mul_mat_q4k(A, B)
    ref <- crossprod(A, B)  # t(A) %*% B, the true (unquantized) product

    # shape / sanity
    expect_equal(dim(res), c(nA, nB))
    expect_true(all(is.finite(res)))
    expect_true(max(abs(res)) > 0)  # not the silently-zero (uninit fp16 table) failure

    # Tracks the true product (convention + correctness): a transpose or a
    # broken kernel tanks the correlation, silently-zero output fails max(abs)
    # above. The residual gap is q4_K's intrinsic ~4.5-bit weight-quantization
    # error (measured cor ~0.998, relative Frobenius ~0.06-0.07 on gaussian
    # weights); the thresholds sit just below that floor with margin. The
    # AVX2/NEON/scalar kernels agree to ~1e-4 (test_simd.R), negligible here.
    expect_true(cor(as.vector(res), as.vector(ref)) > 0.995)

    rel_err <- sqrt(sum((res - ref)^2) / sum(ref^2))
    expect_true(rel_err < 0.09)
}

message("Quantized Q4_K mul_mat tests completed")
