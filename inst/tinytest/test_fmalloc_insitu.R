library(tinytest)
library(Rfmalloc)

message("Testing in-place (by-reference) mutation...")

(function() {
    message("  Test 1: fmalloc_set/fmalloc_fill values and recycling")
    tmp <- tempfile(fileext = ".bin")
    rt <- open_fmalloc(tmp, mode = "scratch", size_gb = 0.2)
    on.exit({ cleanup_fmalloc(rt); unlink(tmp) }, add = TRUE)

    x <- create_fmalloc_vector("numeric", 6, runtime = rt)
    expect_identical(fmalloc_fill(x, 0), x)          # returns x
    expect_true(all(x[] == 0))
    fmalloc_set(x, c(1, 3, 5), c(10, 30, 50))
    expect_identical(x[], c(10, 0, 30, 0, 50, 0))
    fmalloc_set(x, c(2, 4), 99)                       # recycled scalar
    expect_identical(x[], c(10, 99, 30, 99, 50, 0))
})()

(function() {
    message("  Test 2: mutation is by-reference (aliasing, no copy)")
    tmp <- tempfile(fileext = ".bin")
    rt <- open_fmalloc(tmp, mode = "scratch", size_gb = 0.2)
    on.exit({ cleanup_fmalloc(rt); unlink(tmp) }, add = TRUE)

    x <- create_fmalloc_vector("numeric", 4, runtime = rt)
    fmalloc_fill(x, 1)
    y <- x                                           # no copy on assignment
    fmalloc_set(x, 2, 42)
    expect_equal(y[2], 42)                           # alias sees the change
    expect_equal(x[2], 42)
})()

(function() {
    message("  Test 3: all supported atomic types")
    tmp <- tempfile(fileext = ".bin")
    rt <- open_fmalloc(tmp, mode = "scratch", size_gb = 0.2)
    on.exit({ cleanup_fmalloc(rt); unlink(tmp) }, add = TRUE)

    xi <- create_fmalloc_vector("integer", 4, runtime = rt)
    fmalloc_fill(xi, 7L); expect_true(all(xi[] == 7L))
    xl <- create_fmalloc_vector("logical", 3, runtime = rt)
    fmalloc_set(xl, c(1, 3), c(TRUE, FALSE)); expect_identical(xl[c(1, 3)], c(TRUE, FALSE))
    xc <- create_fmalloc_vector("complex", 3, runtime = rt)
    fmalloc_set(xc, 2, 1 + 2i); expect_equal(xc[2], 1 + 2i)
    xr <- create_fmalloc_vector("raw", 4, runtime = rt)
    fmalloc_fill(xr, as.raw(255)); expect_true(all(xr[] == as.raw(255)))

    # column-major linear index into a matrix
    m <- create_fmalloc_matrix("numeric", nrow = 2, ncol = 3, runtime = rt)
    fmalloc_fill(m, 0); fmalloc_set(m, 4, 42)
    expect_equal(m[2, 2], 42)
})()

(function() {
    message("  Test 4: in-place write updates the durable (persistent) store")
    tmp <- tempfile(fileext = ".bin")
    rt <- open_fmalloc(tmp, mode = "persistent")
    on.exit({ cleanup_fmalloc(rt); unlink(tmp) }, add = TRUE)

    x <- create_fmalloc_vector("integer", 4, runtime = rt)
    fmalloc_fill(x, 0L)
    fmalloc_set(x, c(1, 4), c(11L, 44L))
    blob <- serialize(x, NULL)                        # by-reference for persistent
    recovered <- unserialize(blob)
    expect_identical(recovered[], c(11L, 0L, 0L, 44L))
})()

(function() {
    message("  Test 5: validation and error paths")
    tmp <- tempfile(fileext = ".bin")
    rt <- open_fmalloc(tmp, mode = "scratch", size_gb = 0.2)
    on.exit({ cleanup_fmalloc(rt); unlink(tmp) }, add = TRUE)

    x <- create_fmalloc_vector("numeric", 5, runtime = rt)
    fmalloc_fill(x, 0)
    expect_error(fmalloc_set(x, 99, 1), "out of bounds")
    expect_error(fmalloc_set(x, 0, 1), "out of bounds")
    expect_error(fmalloc_set(x, 1, "a"), "atomic")
    expect_error(fmalloc_set(x, c(1, 2), c(1, 2, 3)), "length 1 or length")
    expect_error(fmalloc_fill(x, c(1, 2)), "single")
    expect_error(fmalloc_set(1:5, 1, 1), "fmalloc-backed")

    # character/list vectors are not writable this way
    ch <- create_fmalloc_vector("character", 2, runtime = rt)
    expect_error(fmalloc_set(ch, 1, 1), "atomic")

    # returns invisibly
    expect_false(withVisible(fmalloc_fill(x, 1))$visible)
})()

message("in-place mutation tests completed")
