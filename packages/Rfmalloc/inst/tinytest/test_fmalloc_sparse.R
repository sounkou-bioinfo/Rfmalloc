library(tinytest)
library(Rfmalloc)

message("Testing the sparse tensor codec...")

.sc_gen <- function(nr, nc, dens = 0.1, seed = 1) {
    set.seed(seed)
    M <- matrix(0, nr, nc)
    k <- round(nr * nc * dens)
    idx <- sample(nr * nc, k)
    M[idx] <- rpois(k, 3) + 1
    M
}

(function() {
    message("  Test 1: 'sparse' codec is registered")
    expect_true("sparse" %in% fmalloc_tensor_codecs())
})()

(function() {
    message("  Test 2: lossless round-trip and compression")
    tmp <- tempfile(fileext = ".bin")
    rt <- open_fmalloc(tmp, mode = "persistent")
    on.exit({ cleanup_fmalloc(rt); unlink(tmp) }, add = TRUE)

    X <- .sc_gen(3000, 40, dens = 0.08, seed = 2)
    ten <- as_fmalloc_tensor(X, dtype = "sparse", runtime = rt)
    expect_true(inherits(ten, "fmalloc_tensor"))
    expect_equal(fmalloc_tensor_dtype(ten), "sparse")
    expect_equal(dim(ten), c(3000L, 40L))

    back <- matrix(fmalloc_tensor_materialize(ten)[], 3000, 40)
    expect_identical(as.vector(back), as.vector(X))
    # sparse container beats dense f64 substantially at 8% density
    expect_true(length(unclass(ten)) / (length(X) * 8) < 0.3)

    # dense (all-nonzero) input still round-trips (worst case)
    D <- matrix(rnorm(1100 * 3), 1100, 3)
    tenD <- as_fmalloc_tensor(D, dtype = "sparse", runtime = rt)
    expect_identical(as.vector(fmalloc_tensor_materialize(tenD)[]), as.vector(D))
})()

(function() {
    message("  Test 3: matrix products match base R (both operand sides)")
    tmp <- tempfile(fileext = ".bin")
    rt <- open_fmalloc(tmp, mode = "persistent")
    on.exit({
        cleanup_fmalloc(rt); unlink(tmp)
        options(Rfmalloc.tensor_panel_elems = NULL)
    }, add = TRUE)

    X <- .sc_gen(2048, 30, dens = 0.1, seed = 3)
    ten <- as_fmalloc_tensor(X, dtype = "sparse", runtime = rt)

    b <- matrix(rnorm(30 * 4), 30, 4)
    z <- ten %*% b
    expect_true(is_fmalloc_vector(z))
    expect_equal(as.vector(z[]), as.vector(X %*% b))

    d <- matrix(rnorm(5 * 2048), 5, 2048)
    expect_equal(as.vector((d %*% ten)[]), as.vector(d %*% X))

    # tiny panels exercise multi-chunk streaming
    options(Rfmalloc.tensor_panel_elems = 2048)
    expect_equal(as.vector((ten %*% b)[]), as.vector(X %*% b))
})()

(function() {
    message("  Test 4: non-finite values round-trip and take the NA-safe path")
    tmp <- tempfile(fileext = ".bin")
    rt <- open_fmalloc(tmp, mode = "persistent")
    on.exit({ cleanup_fmalloc(rt); unlink(tmp) }, add = TRUE)

    X <- .sc_gen(1030, 5, dens = 0.1, seed = 4)
    X[c(1L, 500L)] <- c(NA_real_, Inf)
    ten <- as_fmalloc_tensor(X, dtype = "sparse", runtime = rt)
    expect_identical(as.vector(fmalloc_tensor_materialize(ten)[]), as.vector(X))

    b <- matrix(rnorm(5 * 2), 5, 2)
    expect_equal(as.vector((ten %*% b)[]), as.vector(X %*% b))
})()

message("sparse codec tests completed")
