library(tinytest)
library(Rggml)

# CUDA is opt-in. A normal build must expose the probe and decline CUDA
# products cleanly; a CUDA build must compute both dense and quantized products
# through GGML's backend-owned device buffers.

info <- rggml_cuda_info()
expect_true(is.list(info))
expect_equal(names(info), c("n_devices", "device"))
expect_true(is.integer(info$n_devices) && info$n_devices >= 0L)

set.seed(42L)
k <- 64L; n <- 5L; m <- 3L
A <- matrix(rnorm(k * n), k, n)
B <- matrix(rnorm(k * m), k, m)
cpu <- rggml_mul_mat(A, B, backend = "cpu")
expect_equal(cpu, crossprod(A, B), tolerance = 1e-5)

if (!rggml_has_cuda()) {
    expect_equal(info$n_devices, 0L)
    expect_true(is.na(info$device))
    expect_error(rggml_mul_mat(A, B, backend = "cuda"), "unavailable")
    exit_file("CUDA backend not built/available - degradation contract verified")
}

expect_true(is.character(info$device) && nchar(info$device) > 0L)

cuda <- rggml_mul_mat(A, B, backend = "cuda")
expect_equal(dim(cuda), c(n, m))
expect_true(all(is.finite(cuda)))
expect_equal(cuda, cpu, tolerance = 1e-4)

# Exercise enough rows and columns to leave the trivial one-tile path.
set.seed(7L)
A2 <- matrix(rnorm(256L * 33L), 256L, 33L)
B2 <- matrix(rnorm(256L * 17L), 256L, 17L)
expect_equal(
    rggml_mul_mat(A2, B2, backend = "cuda"),
    rggml_mul_mat(A2, B2, backend = "cpu"),
    tolerance = 1e-3
)

# Same device-residency path with actual Q4_K bytes. Compare CUDA with CPU
# after quantization, so the assertion isolates backend correctness from the
# expected quantization error against the original full-precision matrix.
qcpu <- Rggml:::rggml_test_mul_mat_q4k(A2, B2, backend = "cpu")
qcuda <- Rggml:::rggml_test_mul_mat_q4k(A2, B2, backend = "cuda")
expect_true(all(is.finite(qcuda)))
nmse <- sum((qcuda - qcpu)^2) / sum(qcpu^2)
expect_true(nmse <= 5e-4)

message("CUDA device: ", info$device)
