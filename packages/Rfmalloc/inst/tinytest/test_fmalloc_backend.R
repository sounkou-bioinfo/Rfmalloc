library(tinytest)
library(Rfmalloc)

message("Testing the pluggable matmul backend registry...")

(function() {
    message("  Test 1: default is BLAS; unknown backend errors")
    expect_equal(fmalloc_matmul_backend(), "blas")
    expect_error(fmalloc_matmul_backend("does_not_exist"),
                 "no registered matmul backend")
    expect_equal(fmalloc_matmul_backend(), "blas")   # unchanged after error
})()

(function() {
    message("  Test 2: a registered backend receives the matrix products")
    # Register the built-in test backend (computes 2 * A B) via the C-callable.
    invisible(.Call("rfm_register_test_backend_impl"))
    expect_true("test_scale2" %in% fmalloc_matmul_backends())

    tmp <- tempfile(fileext = ".bin")
    rt <- open_fmalloc(tmp, mode = "scratch", size_gb = 0.5)
    on.exit({
        fmalloc_matmul_backend("blas")
        cleanup_fmalloc(rt); unlink(tmp)
        options(Rfmalloc.ooc_threshold_gb = NULL, Rfmalloc.ooc_tile_mb = NULL)
    }, add = TRUE)

    set.seed(1)
    ba <- matrix(rnorm(150 * 60), 150, 60)
    bb <- matrix(rnorm(60 * 20), 60, 20)
    A <- create_fmalloc_matrix("numeric", 150, 60, runtime = rt); A[] <- ba
    B <- create_fmalloc_matrix("numeric", 60, 20, runtime = rt); B[] <- bb

    expect_equal(fmalloc_matmul_backend("test_scale2"), "test_scale2")
    expect_equal(fmalloc_matmul_backend(), "test_scale2")

    # in-core %*% and crossprod route through the backend (doubled)
    expect_equal(as.vector((A %*% B)[]), 2 * as.vector(ba %*% bb))
    expect_equal(as.vector(crossprod(A)[]), 2 * as.vector(crossprod(ba)))

    # out-of-core path routes through rfm_gemm -> backend too
    options(Rfmalloc.ooc_threshold_gb = 0, Rfmalloc.ooc_tile_mb = 0.01)
    expect_equal(as.vector((A %*% B)[]), 2 * as.vector(ba %*% bb))
    options(Rfmalloc.ooc_threshold_gb = NULL, Rfmalloc.ooc_tile_mb = NULL)

    # selecting blas restores exact results
    fmalloc_matmul_backend("blas")
    expect_equal(as.vector((A %*% B)[]), as.vector(ba %*% bb))
})()

(function() {
    message("  Test 3: codec-aware typed backend consumes the raw payload")
    invisible(.Call("rfm_register_test_typed_backend_impl"))
    expect_true("test_typed_f32" %in% fmalloc_matmul_backends())

    tmp <- tempfile(fileext = ".bin")
    rt <- open_fmalloc(tmp, mode = "scratch", size_gb = 0.5)
    on.exit({
        fmalloc_matmul_backend("blas")
        cleanup_fmalloc(rt); unlink(tmp)
    }, add = TRUE)

    # build an f32 typed tensor
    v <- round(rnorm(200 * 40), 3)
    bytes <- writeBin(as.numeric(v), raw(), size = 4L, endian = "little")
    payload <- create_fmalloc_vector("raw", length(bytes), runtime = rt,
                                     zero_initialize = FALSE)
    payload[] <- bytes
    ten <- create_fmalloc_tensor(payload, "f32", c(200L, 40L))
    mf <- matrix(fmalloc_tensor_materialize(ten)[], 200, 40)  # f32-precision ref
    b <- matrix(rnorm(40 * 3), 40, 3)
    d <- matrix(rnorm(5 * 200), 5, 200)

    # select the typed backend: it reads the raw f32 payload and returns 2x
    fmalloc_matmul_backend("test_typed_f32")
    expect_equal(as.vector((ten %*% b)[]), 2 * as.vector(mf %*% b))
    expect_equal(as.vector((d %*% ten)[]), 2 * as.vector(d %*% mf))

    # a codec the typed backend declines (alp) falls back to the exact panel path
    ta <- as_fmalloc_tensor(matrix(round(runif(2048 * 5, 0, 2), 3), 2048, 5),
                            dtype = "alp", runtime = rt)
    mfa <- matrix(fmalloc_tensor_materialize(ta)[], 2048, 5)
    ba <- matrix(rnorm(5 * 2), 5, 2)
    expect_equal(as.vector((ta %*% ba)[]), as.vector(mfa %*% ba))  # exact, not 2x
})()

message("matmul backend tests completed")
