library(tinytest)
library(Rfmalloc)

message("Testing the ALP compressed tensor codec...")

(function() {
    message("  Test 1: lossless round-trip on decimal-scaled data")
    tmp <- tempfile(fileext = ".bin")
    rt <- open_fmalloc(tmp, mode = "persistent")
    on.exit({
        cleanup_fmalloc(rt)
        unlink(tmp)
    }, add = TRUE)

    set.seed(31)
    # Dosage-like decimal data: 3 decimals in [0, 2], multiple chunks + tail.
    x <- matrix(round(runif(1024 * 5 + 17, 0, 2), 3), ncol = 1)
    dim(x) <- c(nrow(x), 1L)
    ten <- as_fmalloc_tensor(x, runtime = rt)
    expect_true(inherits(ten, "fmalloc_tensor"))
    expect_equal(fmalloc_tensor_dtype(ten), "alp")

    back <- fmalloc_tensor_materialize(ten)
    expect_identical(as.vector(back[]), as.vector(x))

    ratio <- length(unclass(ten)) / (length(x) * 8)
    expect_true(ratio < 0.5)

    # Values with full mantissas fall back to raw chunks but stay lossless.
    y <- matrix(rnorm(1024 + 100), ncol = 1)
    ten_y <- as_fmalloc_tensor(y, runtime = rt)
    expect_identical(as.vector(fmalloc_tensor_materialize(ten_y)[]), as.vector(y))
})()

(function() {
    message("  Test 2: non-finite values round-trip and matmul stays base-consistent")
    tmp <- tempfile(fileext = ".bin")
    rt <- open_fmalloc(tmp, mode = "persistent")
    on.exit({
        cleanup_fmalloc(rt)
        unlink(tmp)
    }, add = TRUE)

    x <- matrix(round(runif(2048 * 3, -5, 5), 2), nrow = 2048)
    x[c(1L, 1500L, 4000L)] <- c(NA_real_, NaN, Inf)
    ten <- as_fmalloc_tensor(x, runtime = rt)
    back <- fmalloc_tensor_materialize(ten)
    expect_identical(as.vector(back[]), as.vector(x))

    b <- matrix(round(runif(3 * 4), 2), nrow = 3)
    z <- ten %*% b
    expect_equal(as.vector(z), as.vector(x %*% b))
})()

(function() {
    message("  Test 3: panel-streamed matmul matches base on clean data")
    tmp <- tempfile(fileext = ".bin")
    rt <- open_fmalloc(tmp, mode = "persistent")
    on.exit({
        cleanup_fmalloc(rt)
        unlink(tmp)
        options(Rfmalloc.tensor_panel_elems = NULL)
    }, add = TRUE)

    set.seed(32)
    # nrow is a multiple of the 1024-element chunk so panels stay aligned.
    x <- matrix(round(runif(1024 * 6, 0, 2), 3), nrow = 1024, ncol = 6)
    ten <- as_fmalloc_tensor(x, runtime = rt)

    d <- matrix(round(runif(4 * 1024), 2), nrow = 4)
    z_one <- d %*% ten
    expect_true(inherits(z_one, "fmalloc_matrix"))
    expect_equal(as.vector(z_one), as.vector(d %*% x))

    options(Rfmalloc.tensor_panel_elems = 1024)
    z_many <- d %*% ten
    expect_equal(as.vector(z_many), as.vector(z_one[]))

    b <- matrix(round(runif(6 * 3), 2), nrow = 6)
    z_left <- ten %*% b
    expect_equal(as.vector(z_left), as.vector(x %*% b))
})()

(function() {
    message("  Test 4: plain vectors encode as n x 1 tensors; type errors are clear")
    tmp <- tempfile(fileext = ".bin")
    rt <- open_fmalloc(tmp, mode = "persistent")
    on.exit({
        cleanup_fmalloc(rt)
        unlink(tmp)
    }, add = TRUE)

    v <- round(runif(100), 2)
    ten <- as_fmalloc_tensor(v, runtime = rt)
    expect_equal(dim(ten), c(100L, 1L))
    expect_identical(as.vector(fmalloc_tensor_materialize(ten)[]), v)

    expect_error(as_fmalloc_tensor(1:5, runtime = rt), "must be a double")
    expect_error(as_fmalloc_tensor(v, dtype = "f32", runtime = rt),
        "dtype must be")
})()

message("ALP codec tests completed")
