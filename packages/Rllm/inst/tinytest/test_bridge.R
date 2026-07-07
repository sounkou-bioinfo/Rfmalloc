library(tinytest)
library(Rllm)

# Rllm registers Rggml as Rfmalloc's codec-aware ("typed") matmul backend and
# selects it on load: a product `dense %*% quantized_tensor` is handed to ggml,
# which points a GGML tensor at the fmalloc payload zero-copy and contracts each
# quantized weight row through its SIMD-dispatched vec_dot, quantizing the dense
# operand on the fly. This test drives the whole chain from R.

expect_true(rllm_backend_enabled())  # selected on load

rt <- Rfmalloc::open_fmalloc(tempfile(fileext = ".bin"))

set.seed(1L)

## 1) The q4_K native product tracks the true (unquantized) product to q4_K
##    accuracy. This exercises the runtime-SIMD-dispatched q4_K path end to end
##    over the memory-mapped payload. (We compare against the true product, not
##    Rfmalloc's decode path: Rgguf's q4_K/q2_K decoder is currently inconsistent
##    with GGML's - see test 2, which cross-validates the bridge arithmetic
##    against a type whose decoder IS reliable.)
for (d in list(c(k = 256L, n = 5L, m = 3L),
               c(k = 512L, n = 8L, m = 4L))) {
    k <- d[["k"]]; n <- d[["n"]]; m <- d[["m"]]

    W <- matrix(rnorm(k * n, sd = 0.4), nrow = k, ncol = n)  # weights (k x n)
    X <- matrix(rnorm(m * k),           nrow = m, ncol = k)  # activations (m x k)

    Wt <- rllm_quantize_tensor(W, "q4_k", runtime = rt)
    expect_inherits(Wt, "fmalloc_tensor")
    expect_equal(dim(Wt), c(k, n))
    expect_equal(Rfmalloc::fmalloc_tensor_dtype(Wt), "q4_k")

    Y  <- X %*% Wt   # ggml native quantized product (backend active)
    Yt <- X %*% W    # true (unquantized) product

    expect_equal(dim(Y), c(m, n))
    expect_true(all(is.finite(Y)))
    expect_true(max(abs(Y)) > 0)                 # not the silently-zero fp16-table failure
    expect_true(cor(as.vector(Y), as.vector(Yt)) > 0.99)
    expect_true(sqrt(sum((Y - Yt)^2) / sum(Yt^2)) < 0.10)   # q4_K accuracy
}

## 2) Cross-validate the bridge arithmetic (including the input/output
##    transposes) against an INDEPENDENT decoder, using q8_0 whose Rfmalloc/Rgguf
##    decode is reliable (~8-bit; verified rel < 0.01 against the original). Both
##    paths decode the same weights; ggml additionally quantizes the activations
##    to 8-bit, so the results agree tightly. A transpose bug would collapse the
##    correlation.
k <- 256L; n <- 6L; m <- 4L
W <- matrix(rnorm(k * n, sd = 0.4), nrow = k, ncol = n)
X <- matrix(rnorm(m * k),           nrow = m, ncol = k)
Wt <- rllm_quantize_tensor(W, "q8_0", runtime = rt)

rllm_use_ggml(TRUE)
Yg <- X %*% Wt
rllm_use_ggml(FALSE)
Yr <- X %*% Wt
rllm_use_ggml(TRUE)

expect_equal(dim(Yg), c(m, n))
expect_true(cor(as.vector(Yg), as.vector(Yr)) > 0.999)
expect_true(sqrt(sum((Yg - Yr)^2) / sum(Yr^2)) < 0.02)

## 3) The declined orientation (tensor on the left, T %*% D) is served by
##    Rfmalloc's fallback and must be identical whether or not the ggml backend
##    is selected.
D <- matrix(rnorm(n * 3L), nrow = n, ncol = 3L)  # (n x 3), Wt %*% D is (k x 3)
rllm_use_ggml(TRUE)
Yl_on <- Wt %*% D
rllm_use_ggml(FALSE)
Yl_off <- Wt %*% D
rllm_use_ggml(TRUE)
expect_equal(Yl_on, Yl_off)

message("Rllm typed-GEMM bridge tests completed")
