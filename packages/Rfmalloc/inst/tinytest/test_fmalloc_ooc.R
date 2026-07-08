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

(function() {
    message("  Test 5: out-of-core crossprod matches base and auto-routes")
    tmp <- tempfile(fileext = ".bin")
    rt <- open_fmalloc(tmp, mode = "scratch", size_gb = 0.5)
    on.exit({
        cleanup_fmalloc(rt)
        unlink(tmp)
        options(Rfmalloc.ooc_threshold_gb = NULL, Rfmalloc.ooc_tile_mb = NULL)
    }, add = TRUE)

    set.seed(6)
    m <- 300L; n <- 90L
    X <- create_fmalloc_matrix("numeric", nrow = m, ncol = n, runtime = rt)
    bx <- matrix(rnorm(m * n), m, n)
    X[] <- bx

    # explicit, tiny panels -> many panel pairs + eviction
    C <- fmalloc_crossprod_ooc(X, tile_mb = 0.01)
    expect_true(inherits(C, "fmalloc_matrix"))
    expect_true(is_fmalloc_vector(C))
    expect_equal(dim(C), c(n, n))
    expect_equal(as.vector(C[]), as.vector(crossprod(bx)))
    # symmetric
    expect_equal(as.vector(C[]), as.vector(t(matrix(C[], n, n))))

    # auto-route through crossprod() above the (zeroed) threshold
    options(Rfmalloc.ooc_threshold_gb = 0, Rfmalloc.ooc_tile_mb = 0.01)
    Cr <- crossprod(X)
    expect_equal(as.vector(Cr[]), as.vector(crossprod(bx)))

    # two-arg crossprod still uses the in-core path (not auto-routed)
    Y <- create_fmalloc_matrix("numeric", nrow = m, ncol = 4L, runtime = rt)
    by <- matrix(rnorm(m * 4), m, 4)
    Y[] <- by
    expect_equal(as.vector(crossprod(X, Y)[]), as.vector(crossprod(bx, by)))

    # dimnames propagate to both axes of the Gram matrix
    dimnames(X) <- list(NULL, paste0("v", seq_len(n)))
    Cn <- fmalloc_crossprod_ooc(X, tile_mb = 0.01)
    expect_equal(dimnames(Cn), list(paste0("v", seq_len(n)), paste0("v", seq_len(n))))
})()

(function() {
    message("  Test 6: out-of-core tcrossprod matches base and auto-routes")
    tmp <- tempfile(fileext = ".bin")
    rt <- open_fmalloc(tmp, mode = "scratch", size_gb = 0.5)
    on.exit({
        cleanup_fmalloc(rt)
        unlink(tmp)
        options(Rfmalloc.ooc_threshold_gb = NULL, Rfmalloc.ooc_tile_mb = NULL)
    }, add = TRUE)

    set.seed(7)
    m <- 70L; n <- 200L
    X <- create_fmalloc_matrix("numeric", nrow = m, ncol = n, runtime = rt)
    bx <- matrix(rnorm(m * n), m, n)
    X[] <- bx

    C <- fmalloc_tcrossprod_ooc(X, tile_mb = 0.01)   # single streaming pass
    expect_true(inherits(C, "fmalloc_matrix"))
    expect_true(is_fmalloc_vector(C))
    expect_equal(dim(C), c(m, m))
    expect_equal(as.vector(C[]), as.vector(tcrossprod(bx)))
    expect_equal(as.vector(C[]), as.vector(t(matrix(C[], m, m))))  # symmetric

    options(Rfmalloc.ooc_threshold_gb = 0, Rfmalloc.ooc_tile_mb = 0.01)
    Cr <- tcrossprod(X)
    expect_equal(as.vector(Cr[]), as.vector(tcrossprod(bx)))

    # two-arg tcrossprod keeps the in-core path
    Y <- create_fmalloc_matrix("numeric", nrow = 5L, ncol = n, runtime = rt)
    by <- matrix(rnorm(5 * n), 5, n)
    Y[] <- by
    expect_equal(as.vector(tcrossprod(X, Y)[]), as.vector(tcrossprod(bx, by)))

    dimnames(X) <- list(paste0("s", seq_len(m)), NULL)
    Cn <- fmalloc_tcrossprod_ooc(X, tile_mb = 0.01)
    expect_equal(dimnames(Cn), list(paste0("s", seq_len(m)), paste0("s", seq_len(m))))
})()

message("out-of-core matrix product tests completed")

(function() {
    message("  Test: Gram blocking is invisible, and the fused column sums are exact")
    # crossprod(X) out-of-core chooses its blocking from the shape: row blocks
    # (X read once) when the n x n result fits the tile budget, column panels
    # (X re-read per panel) otherwise. Which one ran must not be observable, so
    # the Gram may not depend on the tile budget, and the column sums accumulated
    # during the sweep must equal the separate reduction they replace.
    tmp <- tempfile(fileext = ".bin")
    rt <- open_fmalloc(tmp, mode = "scratch", size_gb = 0.5)
    on.exit({
        cleanup_fmalloc(rt)
        unlink(tmp)
    }, add = TRUE)

    shapes <- list(c(4000L, 16L),   # tall:       gram << tile, row blocks
                   c(300L, 200L),   # wide:       gram > small tile, column panels
                   c(500L, 1L),     # degenerate n
                   c(1L, 8L))       # degenerate m
    for (shape in shapes) {
        m <- shape[1L]
        n <- shape[2L]
        X <- create_fmalloc_matrix("numeric", nrow = m, ncol = n, runtime = rt)
        set.seed(11L)
        xd <- matrix(rnorm(m * n), m, n)
        X[] <- xd
        gref <- crossprod(xd)
        csref <- base::colSums(xd)

        # 1/64 MB drives the block height down to a handful of rows, and pushes
        # the 300x200 case onto the column-panel fallback.
        for (tile in c(256, 1, 1 / 64)) {
            r <- Rfmalloc:::.fmalloc_gram_ooc(X, tile_mb = tile, colsums = TRUE)
            lab <- sprintf("m=%d n=%d tile=%g", m, n, tile)
            expect_equal(matrix(r$gram[], n, n), gref, tolerance = 1e-8, info = lab)
            # Long double accumulation in file order, so this is bitwise, not close.
            expect_identical(r$colsums, csref, info = lab)
        }

        r0 <- Rfmalloc:::.fmalloc_gram_ooc(X, tile_mb = 1, colsums = FALSE)
        expect_null(r0$colsums)
        expect_equal(matrix(r0$gram[], n, n), gref, tolerance = 1e-8)
        # The public wrapper is unchanged: still just the Gram matrix.
        expect_equal(matrix(fmalloc_crossprod_ooc(X, tile_mb = 1)[], n, n),
                     gref, tolerance = 1e-8)
    }
})()
