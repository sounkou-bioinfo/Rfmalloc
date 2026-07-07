library(tinytest)
library(Rfmalloc)

message("Testing genomics layer (PCA, colVars/rowVars)...")

.align <- function(a, b) sweep(a, 2, sign(colSums(a * b)), "*")

(function() {
    message("  Test 1: colVars / rowVars match base")
    tmp <- tempfile(fileext = ".bin")
    rt <- open_fmalloc(tmp, mode = "scratch", size_gb = 0.2)
    on.exit({ cleanup_fmalloc(rt); unlink(tmp) }, add = TRUE)

    set.seed(1)
    bx <- matrix(rnorm(400 * 30), 400, 30)
    X <- create_fmalloc_matrix("numeric", 400, 30, runtime = rt); X[] <- bx

    expect_equal(fmalloc_colVars(X), apply(bx, 2, var))
    expect_equal(fmalloc_rowVars(X), apply(bx, 1, var))
})()

(function() {
    message("  Test 2: PCA matches prcomp (centered)")
    tmp <- tempfile(fileext = ".bin")
    rt <- open_fmalloc(tmp, mode = "scratch", size_gb = 0.2)
    on.exit({ cleanup_fmalloc(rt); unlink(tmp) }, add = TRUE)

    set.seed(2)
    m <- 600L; n <- 40L
    bx <- matrix(rnorm(m * n) + rep(rnorm(n) * 2, each = m), m, n)
    X <- create_fmalloc_matrix("numeric", m, n, runtime = rt); X[] <- bx

    p  <- fmalloc_pca(X, k = 6)
    pr <- prcomp(bx, center = TRUE)

    expect_equal(p$sdev, pr$sdev[1:6], tolerance = 1e-6)
    expect_equal(.align(p$x, pr$x[, 1:6]), pr$x[, 1:6, drop = FALSE],
                 tolerance = 1e-6, check.attributes = FALSE)
    expect_equal(.align(p$rotation, pr$rotation[, 1:6]), pr$rotation[, 1:6, drop = FALSE],
                 tolerance = 1e-6, check.attributes = FALSE)
    expect_equal(as.numeric(p$center), as.numeric(colMeans(bx)))
})()

(function() {
    message("  Test 3: uncentered PCA matches svd; k clamps to n")
    tmp <- tempfile(fileext = ".bin")
    rt <- open_fmalloc(tmp, mode = "scratch", size_gb = 0.2)
    on.exit({ cleanup_fmalloc(rt); unlink(tmp) }, add = TRUE)

    set.seed(3)
    m <- 300L; n <- 20L
    bx <- matrix(rnorm(m * n), m, n)
    X <- create_fmalloc_matrix("numeric", m, n, runtime = rt); X[] <- bx

    p <- fmalloc_pca(X, k = 100, center = FALSE)  # k clamps to n
    expect_equal(ncol(p$rotation), n)
    sv <- svd(bx)
    expect_equal(p$sdev[1:5], (sv$d / sqrt(m - 1))[1:5], tolerance = 1e-6)
    expect_false(is.numeric(p$center))            # FALSE when not centered
})()

(function() {
    message("  Test 4: PCA is backend-composable (routes through the registry)")
    invisible(.Call("rfm_register_test_backend_impl"))
    tmp <- tempfile(fileext = ".bin")
    rt <- open_fmalloc(tmp, mode = "scratch", size_gb = 0.2)
    on.exit({
        fmalloc_matmul_backend("blas")
        cleanup_fmalloc(rt); unlink(tmp)
    }, add = TRUE)

    set.seed(4)
    bx <- matrix(rnorm(300 * 25), 300, 25)
    X <- create_fmalloc_matrix("numeric", 300, 25, runtime = rt); X[] <- bx

    base_pca <- fmalloc_pca(X, k = 4)
    fmalloc_matmul_backend("test_scale2")         # backend doubles every matmul
    routed <- fmalloc_pca(X, k = 4)
    # the heavy ops (Gram + projection) went through the backend, so results change
    expect_false(isTRUE(all.equal(routed$sdev, base_pca$sdev)))
})()

message("genomics layer tests completed")
