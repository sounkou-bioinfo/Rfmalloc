library(tinytest)
library(Rggml)

# The Vulkan backend is opt-in at build time (--with-vulkan), so these tests
# adapt: without it, we assert the graceful-degradation contract; with it, we
# assert that the GPU backend computes the same answer as the CPU backend.
#
# Both paths go through RC_rggml_test_mul_mat_backend, which uses the
# backend-agnostic residency path: build the graph in a no_alloc context, let
# the backend allocate every tensor in one of its own buffers (device memory
# for a GPU), upload inputs, compute, download the result. That single code
# path serves CPU, BLAS and Vulkan - the point of the device-buffer API.
#
# A software Vulkan driver (Mesa lavapipe/llvmpipe) satisfies this test: it is
# slow, but it exercises the real backend, shaders and buffer residency.

info <- rggml_vulkan_info()
expect_true(is.list(info))
expect_true(is.integer(info$n_devices) && info$n_devices >= 0L)

mm <- function(A, B, backend) .Call("RC_rggml_test_mul_mat_backend", A, B, backend)

set.seed(42L)
k <- 64L; n <- 5L; m <- 3L
A <- matrix(rnorm(k * n), k, n)
B <- matrix(rnorm(k * m), k, m)
ref <- crossprod(A, B)          # ggml_mul_mat(A, B) == crossprod(A, B)

# The CPU path through the device-buffer API must agree with the R reference.
cpu <- mm(A, B, 0L)
expect_equal(dim(cpu), c(n, m))
expect_equal(cpu, ref, tolerance = 1e-5)

# BLAS backend, same path.
blas <- mm(A, B, 1L)
expect_equal(blas, ref, tolerance = 1e-5)

if (!rggml_has_vulkan()) {
    # Built without --with-vulkan (or no driver): the contract is that we
    # degrade gracefully rather than crash.
    expect_equal(info$n_devices, 0L)
    expect_true(is.na(info$device))
    expect_error(mm(A, B, 2L), "unavailable")
    exit_file("Vulkan backend not built/available - degradation contract verified")
}

message("Vulkan device: ", info$device)
expect_true(is.character(info$device) && nchar(info$device) > 0)

# The GPU backend must produce the same answer as the CPU one. f32 accumulation
# order differs, so compare to float tolerance, not bit-exactly.
vk <- mm(A, B, 2L)
expect_equal(dim(vk), c(n, m))
expect_true(all(is.finite(vk)))
expect_equal(vk, ref, tolerance = 1e-4)
expect_equal(vk, cpu, tolerance = 1e-4)

# A larger, less symmetric problem: shapes and strides that exercise the
# matmul shader's tiling rather than a trivial one-tile case.
set.seed(7L)
A2 <- matrix(rnorm(256L * 33L), 256L, 33L)
B2 <- matrix(rnorm(256L * 17L), 256L, 17L)
expect_equal(mm(A2, B2, 2L), crossprod(A2, B2), tolerance = 1e-3)

message("Vulkan backend tests completed")
