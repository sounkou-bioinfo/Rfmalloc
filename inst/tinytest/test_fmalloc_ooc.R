library(tinytest)
library(Rfmalloc)

message("Testing out-of-core column-tiled matrix products...")

(function() {
    message("  Test 1: tiled gemv/gemm match base R with many small tiles")
    tmp <- tempfile(fileext = ".bin")
    rt <- open_fmalloc(tmp, mode = "scratch", size_gb = 0.5)
    on.exit({
        cleanup_fmalloc(rt)
        unlink(tmp)
    }, add = TRUE)

    set.seed(3)
    m <- 400L; n <- 250L; k <- 5L
    A <- create_fmalloc_matrix("numeric", nrow = m, ncol = n, runtime = rt)
    ba <- matrix(rnorm(m * n), m, n)
    A[] <- ba

    # tile_mb = 0.02 (~2 columns/tile) forces many tiles + evictions.
    xv <- rnorm(n)
    yv <- fmalloc_matmul_ooc(A, xv, tile_mb = 0.02)
    expect_true(inherits(yv, "fmalloc_matrix"))
    expect_true(is_fmalloc_vector(yv))
    expect_equal(dim(yv), c(m, 1L))
    expect_equal(as.vector(yv[]), as.vector(ba %*% xv))

    X <- matrix(rnorm(n * k), n, k)
    Y <- fmalloc_matmul_ooc(A, X, tile_mb = 0.02)
    expect_equal(dim(Y), c(m, k))
    expect_equal(as.vector(Y[]), as.vector(ba %*% X))
})()

(function() {
    message("  Test 2: result equals the in-core BLAS path exactly")
    tmp <- tempfile(fileext = ".bin")
    rt <- open_fmalloc(tmp, mode = "scratch", size_gb = 0.5)
    on.exit({
        cleanup_fmalloc(rt)
        unlink(tmp)
    }, add = TRUE)

    set.seed(4)
    m <- 128L; n <- 96L
    A <- create_fmalloc_matrix("numeric", nrow = m, ncol = n, runtime = rt)
    A[] <- rnorm(m * n)
    x <- rnorm(n)

    ooc <- fmalloc_matmul_ooc(A, x, tile_mb = 0.01)
    incore <- A %*% x  # regular dgemm path
    expect_equal(as.vector(ooc[]), as.vector(incore[]))
})()

(function() {
    message("  Test 3: edge cases and validation")
    tmp <- tempfile(fileext = ".bin")
    rt <- open_fmalloc(tmp, mode = "scratch", size_gb = 0.2)
    on.exit({
        cleanup_fmalloc(rt)
        unlink(tmp)
    }, add = TRUE)

    A <- create_fmalloc_matrix("numeric", nrow = 10L, ncol = 6L, runtime = rt)
    A[] <- as.numeric(1:60)

    expect_error(fmalloc_matmul_ooc(A, rnorm(5)), "non-conformable")
    expect_error(fmalloc_matmul_ooc(rnorm(10), rnorm(10)), "fmalloc-backed")
    expect_error(fmalloc_matmul_ooc(A, rnorm(6), tile_mb = -1), "positive")

    # advise helper returns the payload byte count
    nb <- .Call("rfm_vector_advise_impl", A, 0L)
    expect_equal(nb, 10 * 6 * 8)
})()

(function() {
    message("  Test 4: %*% auto-routes to OOC above the size threshold")
    tmp <- tempfile(fileext = ".bin")
    rt <- open_fmalloc(tmp, mode = "scratch", size_gb = 0.5)
    on.exit({
        cleanup_fmalloc(rt)
        unlink(tmp)
        options(Rfmalloc.ooc_threshold_gb = NULL, Rfmalloc.ooc_tile_mb = NULL)
    }, add = TRUE)

    set.seed(5)
    m <- 200L; n <- 120L
    A <- create_fmalloc_matrix("numeric", nrow = m, ncol = n, runtime = rt,
                               dimnames = list(NULL, NULL))
    ba <- matrix(rnorm(m * n), m, n)
    A[] <- ba

    b <- matrix(rnorm(n * 3), n, 3)
    incore <- A %*% b            # threshold default Inf-ish -> in-core

    # Force every product through the OOC path with a tiny threshold + tiles.
    options(Rfmalloc.ooc_threshold_gb = 0, Rfmalloc.ooc_tile_mb = 0.02)
    routed <- A %*% b
    expect_true(is_fmalloc_vector(routed))
    expect_equal(as.vector(routed[]), as.vector(ba %*% b))
    expect_equal(as.vector(routed[]), as.vector(incore[]))

    # gemv shape too
    v <- rnorm(n)
    expect_equal(as.vector((A %*% v)[]), as.vector(ba %*% v))

    # dimnames propagate like base
    An <- create_fmalloc_matrix("numeric", nrow = 4L, ncol = 3L, runtime = rt)
    An[] <- as.numeric(1:12)
    dimnames(An) <- list(c("r1","r2","r3","r4"), c("a","b","c"))
    bn <- matrix(as.numeric(1:6), 3, 2, dimnames = list(NULL, c("x","y")))
    rn <- An %*% bn
    expect_equal(dimnames(rn), list(c("r1","r2","r3","r4"), c("x","y")))
})()

message("out-of-core matrix product tests completed")
