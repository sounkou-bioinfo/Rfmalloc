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

(function() {
    message("  Test 6: in-place arithmetic (add/sub/mul/div)")
    tmp <- tempfile(fileext = ".bin")
    rt <- open_fmalloc(tmp, mode = "scratch", size_gb = 0.2)
    on.exit({ cleanup_fmalloc(rt); unlink(tmp) }, add = TRUE)

    x <- create_fmalloc_vector("numeric", 5, runtime = rt)
    fmalloc_fill(x, 10)
    expect_identical(fmalloc_add(x, 5), x)            # returns x
    expect_true(all(x[] == 15))
    fmalloc_mul(x, c(1, 2, 3, 4, 5))                  # vector y
    expect_identical(x[], c(15, 30, 45, 60, 75))
    fmalloc_div(x, 5)
    expect_identical(x[], c(3, 6, 9, 12, 15))
    fmalloc_sub(x, 3)
    expect_identical(x[], c(0, 3, 6, 9, 12))

    # by-reference: aliasing sees arithmetic too
    y <- x
    fmalloc_add(x, 100)
    expect_equal(y[], c(100, 103, 106, 109, 112))

    # NA follows IEEE double semantics
    fmalloc_fill(x, 1); fmalloc_add(x, NA_real_)
    expect_true(all(is.na(x[])))

    # validation
    xi <- create_fmalloc_vector("integer", 3, runtime = rt)
    expect_error(fmalloc_add(xi, 1), "numeric")       # double only
    expect_error(fmalloc_add(x, c(1, 2)), "length 1 or length")
    expect_error(fmalloc_mul(1:5, 2), "fmalloc-backed")
    expect_false(withVisible(fmalloc_add(x, 0))$visible)
})()

(function() {
    message("  Test 7: unshared x[i]<- is already in place; fmalloc_set aliases")
    tmp <- tempfile(fileext = ".bin")
    rt <- open_fmalloc(tmp, mode = "scratch", size_gb = 0.2)
    on.exit({ cleanup_fmalloc(rt); unlink(tmp) }, add = TRUE)

    # ordinary [<- on an UNSHARED fmalloc vector stays fmalloc-backed (no copy
    # to a plain R vector) -- ALTREP writes through the data pointer in place
    x <- create_fmalloc_vector("numeric", 5, runtime = rt)
    x[] <- 1
    x[2] <- 99
    expect_true(is_fmalloc_vector(x))
    expect_equal(x[2], 99)

    # ordinary [<- on a SHARED vector copies (value semantics preserved)
    y <- x
    x[3] <- 42
    expect_equal(y[3], 1)                 # y unchanged -> x was duplicated

    # fmalloc_set mutates by reference even when shared (aliasing)
    z <- x
    fmalloc_set(x, 3, 7)
    expect_equal(z[3], 7)                 # alias observes the change
})()

(function() {
    message("  Test 8: fmalloc_sync flushes without error and returns bytes")
    tmp <- tempfile(fileext = ".bin")
    rt <- open_fmalloc(tmp, mode = "persistent")
    on.exit({ cleanup_fmalloc(rt); unlink(tmp) }, add = TRUE)

    x <- create_fmalloc_vector("integer", 8, runtime = rt)
    fmalloc_fill(x, 5L)
    n <- withVisible(fmalloc_sync(rt))
    expect_false(n$visible)               # invisible
    # on POSIX msync returns the mapped size (> 0); Windows no-op returns 0
    expect_true(n$value >= 0)
    expect_true({ fmalloc_sync(rt, wait = FALSE); TRUE })
    # data still correct after sync
    expect_true(all(x[] == 5L))
})()

message("in-place mutation tests completed")
