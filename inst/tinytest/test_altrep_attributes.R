library(tinytest)
library(Rfmalloc)

message("Testing ALTREP attributes for matrix/array/data.frame objects...")

(function() {
    message("  Test 1: Matrix attributes roundtrip")
    tmp <- tempfile(fileext = ".bin")
    on.exit({
        cleanup_fmalloc(rt)
        unlink(tmp)
    }, add = TRUE)

    rt <- open_fmalloc(tmp)
    v <- create_fmalloc_vector("integer", 6, runtime = rt)
    v[] <- 1:6
    dim(v) <- c(2L, 3L)
    dimnames(v) <- list(c("r1", "r2"), c("c1", "c2", "c3"))

    expect_true(is.matrix(v))
    expect_equal(dim(v), c(2L, 3L))
    expect_equal(dimnames(v)[[1]], c("r1", "r2"))
    expect_equal(dimnames(v)[[2]], c("c1", "c2", "c3"))

    blob <- serialize(v, NULL)
    restored <- unserialize(blob)
    expect_equal(dim(restored), c(2L, 3L))
    expect_equal(dimnames(restored)[[1]], c("r1", "r2"))
    expect_equal(dimnames(restored)[[2]], c("c1", "c2", "c3"))
    expect_equal(as.vector(restored[]), 1:6)
    expect_true(is.integer(restored))

    message("  Matrix attribute roundtrip passed")
})()

(function() {
    message("  Test 2: Array attributes roundtrip")
    tmp <- tempfile(fileext = ".bin")
    on.exit({
        cleanup_fmalloc(rt)
        unlink(tmp)
    }, add = TRUE)

    rt <- open_fmalloc(tmp)
    v <- create_fmalloc_vector("numeric", 24, runtime = rt)
    v[] <- seq_len(24)
    dim(v) <- c(2L, 3L, 4L)
    dimnames(v) <- list(c("x1", "x2"), c("y1", "y2", "y3"), c("z1", "z2", "z3", "z4"))

    expect_true(is.array(v))
    expect_equal(dim(v), c(2L, 3L, 4L))
    expect_equal(dimnames(v)[[3]], c("z1", "z2", "z3", "z4"))

    blob <- serialize(v, NULL)
    restored <- unserialize(blob)
    expect_equal(dim(restored), c(2L, 3L, 4L))
    expect_equal(dimnames(restored)[[3]], c("z1", "z2", "z3", "z4"))
    expect_equal(as.vector(restored[]), seq_len(24))

    message("  Array attribute roundtrip passed")
})()

(function() {
    message("  Test 3: data.frame columns and row.names roundtrip")
    tmp <- tempfile(fileext = ".bin")
    on.exit({
        cleanup_fmalloc(rt)
        unlink(tmp)
    }, add = TRUE)

    rt <- open_fmalloc(tmp)
    a <- create_fmalloc_vector("integer", 3, runtime = rt)
    b <- create_fmalloc_vector("character", 3, runtime = rt)
    a[] <- c(1L, 2L, 3L)
    b[] <- c("aa", "bb", "cc")

    df <- data.frame(a = a, b = b, stringsAsFactors = FALSE)
    expect_equal(class(df), c("data.frame"))
    expect_equal(names(df), c("a", "b"))
    expect_equal(as.integer(row.names(df)), c(1L, 2L, 3L))
    expect_true(all(df$a[] == c(1L, 2L, 3L)))
    expect_equal(as.character(df$b[]), c("aa", "bb", "cc"))

    blob <- serialize(df, NULL)
    restored <- unserialize(blob)
    expect_equal(class(restored), c("data.frame"))
    expect_equal(names(restored), c("a", "b"))
    expect_equal(as.integer(row.names(restored)), c(1L, 2L, 3L))
    expect_equal(restored$a[], c(1L, 2L, 3L))
    expect_equal(restored$b[], c("aa", "bb", "cc"))

    message("  data.frame attribute roundtrip passed")
})()

message("Attribute tests for ALTREP matrix/array/data.frame completed")
