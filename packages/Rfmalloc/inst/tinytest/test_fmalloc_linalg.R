library(tinytest)
library(Rfmalloc)

message("Testing fmalloc matrix algebra methods...")

(function() {
    message("  Test 1: fmalloc matrix multiplication matches base R")
    tmp <- tempfile(fileext = ".bin")
    rt <- open_fmalloc(tmp, mode = "persistent")
    on.exit({
        cleanup_fmalloc(rt)
        unlink(tmp)
    }, add = TRUE)

    x <- create_fmalloc_matrix("numeric", nrow = 2, ncol = 3, runtime = rt)
    y <- create_fmalloc_matrix("numeric", nrow = 3, ncol = 2, runtime = rt)
    x[] <- as.numeric(1:6)
    y[] <- as.numeric(1:6)

    bx <- matrix(as.numeric(1:6), nrow = 2, ncol = 3)
    by <- matrix(as.numeric(1:6), nrow = 3, ncol = 2)

    z <- x %*% y
    expect_true(inherits(z, "fmalloc_matrix"))
    expect_equal(dim(z), c(2L, 2L))
    expect_equal(as.vector(z), as.vector(bx %*% by))

    z_rhs <- bx %*% y
    expect_true(inherits(z_rhs, "fmalloc_matrix"))
    expect_equal(as.vector(z_rhs), as.vector(bx %*% by))

    message("  Matrix multiplication test passed")
})()

(function() {
    message("  Test 2: crossprod and tcrossprod match base R")
    tmp <- tempfile(fileext = ".bin")
    rt <- open_fmalloc(tmp, mode = "persistent")
    on.exit({
        cleanup_fmalloc(rt)
        unlink(tmp)
    }, add = TRUE)

    x <- create_fmalloc_matrix("numeric", nrow = 2, ncol = 3, runtime = rt)
    x[] <- as.numeric(1:6)
    bx <- matrix(as.numeric(1:6), nrow = 2, ncol = 3)

    cp <- crossprod(x)
    expect_true(inherits(cp, "fmalloc_matrix"))
    expect_equal(dim(cp), c(3L, 3L))
    expect_equal(as.vector(cp), as.vector(crossprod(bx)))

    tcp <- tcrossprod(x)
    expect_true(inherits(tcp, "fmalloc_matrix"))
    expect_equal(dim(tcp), c(2L, 2L))
    expect_equal(as.vector(tcp), as.vector(tcrossprod(bx)))

    message("  Cross-product tests passed")
})()

(function() {
    message("  Test 3: vector promotion follows base matrix product behavior")
    tmp <- tempfile(fileext = ".bin")
    rt <- open_fmalloc(tmp, mode = "persistent")
    on.exit({
        cleanup_fmalloc(rt)
        unlink(tmp)
    }, add = TRUE)

    v <- create_fmalloc_vector("numeric", 3, runtime = rt)
    m <- create_fmalloc_matrix("numeric", nrow = 3, ncol = 2, runtime = rt)
    v[] <- as.numeric(1:3)
    m[] <- as.numeric(1:6)

    bv <- as.numeric(1:3)
    bm <- matrix(as.numeric(1:6), nrow = 3, ncol = 2)

    vm <- v %*% m
    expect_true(inherits(vm, "fmalloc_matrix"))
    expect_equal(dim(vm), c(1L, 2L))
    expect_equal(as.vector(vm), as.vector(bv %*% bm))

    cp <- crossprod(v)
    expect_true(inherits(cp, "fmalloc_matrix"))
    expect_equal(dim(cp), c(1L, 1L))
    expect_equal(as.vector(cp), as.vector(crossprod(bv)))

    tcp <- tcrossprod(v)
    expect_true(inherits(tcp, "fmalloc_matrix"))
    expect_equal(dim(tcp), c(3L, 3L))
    expect_equal(as.vector(tcp), as.vector(tcrossprod(bv)))

    message("  Vector promotion tests passed")
})()

(function() {
    message("  Test 4: zero-length and non-conformable edge cases")
    tmp <- tempfile(fileext = ".bin")
    rt <- open_fmalloc(tmp, mode = "persistent")
    on.exit({
        cleanup_fmalloc(rt)
        unlink(tmp)
    }, add = TRUE)

    z0 <- create_fmalloc_vector("numeric", 0L, runtime = rt)
    z1 <- create_fmalloc_vector("numeric", 0L, runtime = rt)
    m02 <- create_fmalloc_matrix("numeric", nrow = 0L, ncol = 2L, runtime = rt)

    c1 <- matrix(c(1, 1), nrow = 2L)

    vm <- z0 %*% z1
    expect_true(inherits(vm, "fmalloc_matrix"))
    expect_equal(dim(vm), c(1L, 1L))

    tcp <- tcrossprod(z0)
    expect_true(inherits(tcp, "fmalloc_matrix"))
    expect_equal(dim(tcp), c(0L, 0L))

    cp <- crossprod(z0)
    expect_true(inherits(cp, "fmalloc_matrix"))
    expect_equal(dim(cp), c(1L, 1L))

    m0 <- m02 %*% c1
    expect_true(inherits(m0, "fmalloc_matrix"))
    expect_equal(dim(m0), c(0L, 1L))

    v02 <- z0 %*% matrix(as.numeric(1:2), nrow = 1, ncol = 2)
    expect_true(inherits(v02, "fmalloc_matrix"))
    expect_equal(dim(v02), c(0L, 2L))

    expect_error(z0 %*% matrix(as.numeric(1:6), nrow = 2, ncol = 3), "non-conformable arguments")

    message("  Zero-length and non-conformable tests passed")
})()

(function() {
    message("  Test 5: type promotion and dimnames are preserved")
    tmp <- tempfile(fileext = ".bin")
    rt <- open_fmalloc(tmp, mode = "persistent")
    on.exit({
        cleanup_fmalloc(rt)
        unlink(tmp)
    }, add = TRUE)

    x <- matrix(c(TRUE, TRUE, FALSE, TRUE), nrow = 2L, dimnames = list(c("r1", "r2"), c("c1", "c2")))
    y <- matrix(c(1 + 1i, 2 - 1i, 3 + 2i, 4 - 2i), nrow = 2L,
                dimnames = list(c("c1", "c2"), c("c3", "c4")))
    fx <- create_fmalloc_matrix("logical", nrow = 2L, ncol = 2L, runtime = rt)
    fx[] <- as.double(x)
    dimnames(fx) <- dimnames(x)
    fy <- create_fmalloc_matrix("complex", nrow = 2L, ncol = 2L, runtime = rt)
    fy[] <- as.complex(y)
    dimnames(fy) <- dimnames(y)

    prod <- fx %*% fy
    expect_true(inherits(prod, "fmalloc_matrix"))
    expect_identical(typeof(prod), "complex")
    expect_identical(as.vector(prod), as.vector(x %*% y))
    expect_equal(dimnames(prod), list(c("r1", "r2"), c("c3", "c4")))

    cprod <- crossprod(fy)
    expect_true(inherits(cprod, "fmalloc_matrix"))
    expect_equal(typeof(cprod), "complex")

    expect_identical(as.vector(cprod), as.vector(crossprod(y)))

    tprod <- tcrossprod(fy)
    expect_true(inherits(tprod, "fmalloc_matrix"))
    expect_equal(dimnames(tprod), list(c("c1", "c2"), c("c1", "c2")))

    expect_identical(as.vector(tprod), as.vector(tcrossprod(y)))

    message("  Type promotion and dimnames tests passed")
})()

(function() {
    message("  Test 6: large referenced operands survive the ALTREP wrapper path")
    tmp <- tempfile(fileext = ".bin")
    rt <- open_fmalloc(tmp, mode = "persistent")
    on.exit({
        cleanup_fmalloc(rt)
        unlink(tmp)
    }, add = TRUE)

    a <- create_fmalloc_matrix("numeric", nrow = 100, ncol = 50, runtime = rt)
    b <- create_fmalloc_matrix("numeric", nrow = 50, ncol = 30, runtime = rt)
    a[] <- as.numeric(seq_len(100 * 50) %% 17)
    b[] <- as.numeric(seq_len(50 * 30) %% 13)

    ba <- matrix(as.numeric(seq_len(100 * 50) %% 17), nrow = 100, ncol = 50)
    bb <- matrix(as.numeric(seq_len(50 * 30) %% 13), nrow = 50, ncol = 30)

    # Second references: attribute changes on referenced ALTREPs of length
    # >= 64 make R substitute a generic wrapper instead of duplicating.
    keep_a <- a
    keep_b <- b

    w <- a
    class(w) <- NULL
    expect_true(is_fmalloc_vector(w))

    z <- a %*% b
    expect_true(inherits(z, "fmalloc_matrix"))
    expect_true(is_fmalloc_vector(z))
    expect_equal(dim(z), c(100L, 30L))
    expect_equal(as.vector(z), as.vector(ba %*% bb))

    cp <- crossprod(a)
    expect_equal(as.vector(cp), as.vector(crossprod(ba)))

    tcp <- tcrossprod(b)
    expect_equal(as.vector(tcp), as.vector(tcrossprod(bb)))

    message("  ALTREP wrapper regression tests passed")
})()

(function() {
    message("  Test 7: BLAS path matches base R; NA/NaN/Inf fall back with base semantics")
    tmp <- tempfile(fileext = ".bin")
    rt <- open_fmalloc(tmp, mode = "persistent")
    on.exit({
        cleanup_fmalloc(rt)
        unlink(tmp)
    }, add = TRUE)

    set.seed(7)
    ba <- matrix(rnorm(200 * 80), nrow = 200, ncol = 80)
    bb <- matrix(rnorm(80 * 120), nrow = 80, ncol = 120)

    a <- create_fmalloc_matrix("numeric", nrow = 200, ncol = 80, runtime = rt)
    b <- create_fmalloc_matrix("numeric", nrow = 80, ncol = 120, runtime = rt)
    a[] <- ba
    b[] <- bb

    z <- a %*% b
    expect_true(inherits(z, "fmalloc_matrix"))
    expect_equal(as.vector(z), as.vector(ba %*% bb))
    expect_equal(as.vector(crossprod(a)), as.vector(crossprod(ba)))
    expect_equal(as.vector(tcrossprod(a)), as.vector(tcrossprod(ba)))

    # Non-finite values must take the managed-loop path and match base.
    ba_na <- ba
    ba_na[c(1L, 300L, 5000L)] <- c(NA_real_, NaN, Inf)
    a_na <- create_fmalloc_matrix("numeric", nrow = 200, ncol = 80, runtime = rt)
    a_na[] <- ba_na

    z_na <- a_na %*% b
    expect_true(inherits(z_na, "fmalloc_matrix"))
    expect_equal(as.vector(z_na), as.vector(ba_na %*% bb))
    expect_equal(as.vector(crossprod(a_na)), as.vector(crossprod(ba_na)))

    message("  BLAS and non-finite fallback tests passed")
})()

message("fmalloc matrix algebra tests completed")
