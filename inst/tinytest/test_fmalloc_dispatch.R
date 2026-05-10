library(tinytest)
library(Rfmalloc)

message("Testing fmalloc S3 dispatch for arithmetic and summary methods")

assert_fmalloc_managed <- function(obj, runtime, class_contains = NULL, before_records = NULL, min_allocation_increase = 1L) {
    expect_true(inherits(obj, "fmalloc"))
    if (!is.null(class_contains)) {
        expect_true(class_contains %in% class(obj))
    }

    inspect <- capture.output(.Internal(inspect(obj)))[1L]
    expect_match(inspect, "fmalloc_altrep")

    after <- nrow(list_fmalloc_allocations(runtime))
    if (!is.null(before_records)) {
        expect_true(after >= before_records + min_allocation_increase)
    }
}

(function() {
    message("Test 1: vector classes are present and arithmetic dispatch exists")
    tmp <- tempfile(fileext = ".bin")
    rt <- open_fmalloc(tmp)
    on.exit({
        cleanup_fmalloc(rt)
        unlink(tmp)
    }, add = TRUE)

    v <- create_fmalloc_vector("integer", 4L, runtime = rt)
    v[] <- 1:4

    expect_true(inherits(v, "fmalloc"))
    expect_true("fmalloc_vector" %in% class(v))
    expect_equal(unclass(v + 1L), 2:5)
    expect_equal(unclass(1L + v), 2:5)
    expect_equal(unclass(v - 1L), 0:3)
    expect_equal(unclass(v * 2L), c(2L, 4L, 6L, 8L))
    expect_equal(unclass(-v), -1L:-4L)
    expect_equal(sum(v), 10)
    expect_equal(min(v), 1L)

    fake <- structure(c(1, 2, 3, 4), class = c("fmalloc_vector", "fmalloc", "numeric"))
    fake_inspect <- capture.output(.Internal(inspect(fake)))[1L]
    expect_false(grepl("fmalloc_altrep", fake_inspect))

    # Non-fallback method dispatch for both argument orders
    expect_equal(sum(v) - 1L, 9)
    expect_equal(unclass(3 + v - 2L), 2:5)

    message("Test 1 passed")
})()

(function() {
    message("Test 2: Ops on large vectors/matrices remain fmalloc-managed")
    tmp <- tempfile(fileext = ".bin")
    rt <- open_fmalloc(tmp)
    on.exit({
        cleanup_fmalloc(rt)
        unlink(tmp)
    }, add = TRUE)

    x <- create_fmalloc_vector("numeric", 100000L, runtime = rt)
    x[] <- seq_len(100000L)

    before_y <- nrow(list_fmalloc_allocations(rt))
    y <- x + 1
    assert_fmalloc_managed(y, rt, "fmalloc_vector", before_records = before_y)
    expect_equal(unclass(y), unclass(x + 1))

    x_named <- create_fmalloc_vector("numeric", 2L, runtime = rt)
    x_named[] <- c(1, 2)
    names(x_named) <- c("left", "right")

    z_named <- create_fmalloc_vector("numeric", 4L, runtime = rt)
    z_named[] <- c(10, 20, 30, 40)
    names(z_named) <- c("a", "b", "c", "d")

    before_w <- nrow(list_fmalloc_allocations(rt))
    w <- x_named + z_named
    assert_fmalloc_managed(w, rt, "fmalloc_vector", before_records = before_w)
    expect_identical(names(w), names(z_named))

    m_base <- matrix(1:20L, nrow = 4L, ncol = 5L)
    dimnames(m_base) <- list(letters[1:4], letters[1:5])
    m <- create_fmalloc_matrix("integer", nrow = 4L, ncol = 5L, runtime = rt)
    m[] <- m_base
    dimnames(m) <- list(letters[1:4], letters[1:5])

    before_m_plus <- nrow(list_fmalloc_allocations(rt))
    m_plus <- m + 10L
    assert_fmalloc_managed(m_plus, rt, "fmalloc_matrix", before_records = before_m_plus)
    expect_identical(unclass(m_plus), unclass(m_base + 10L))

    before_m_neg <- nrow(list_fmalloc_allocations(rt))
    m_neg <- -m
    assert_fmalloc_managed(m_neg, rt, "fmalloc_matrix", before_records = before_m_neg)
    expect_identical(unclass(m_neg), unclass(-m_base))

    vec <- c(1L, 2L)
    before_m_from_vec <- nrow(list_fmalloc_allocations(rt))
    m_from_vec <- m + vec
    assert_fmalloc_managed(m_from_vec, rt, "fmalloc_matrix", before_records = before_m_from_vec)
    expect_identical(unclass(m_from_vec), unclass(m_base + vec))

    a <- create_fmalloc_array("numeric", c(2L, 2L), runtime = rt)
    b <- create_fmalloc_array("numeric", c(1L, 2L, 2L), runtime = rt)
    a[] <- 1:4
    b[] <- 1:4
    expect_error(a + b, "non-conformable arrays")

    message("Test 2 passed")
})()

(function() {
    message("Test 3: matrix methods for rowSums/colSums available")
    tmp <- tempfile(fileext = ".bin")
    rt <- open_fmalloc(tmp)
    on.exit({
        cleanup_fmalloc(rt)
        unlink(tmp)
    }, add = TRUE)

    m <- create_fmalloc_matrix("integer", nrow = 2L, ncol = 3L, runtime = rt)
    m[] <- 1:6

    expect_true(inherits(m, "fmalloc"))
    expect_true("fmalloc_matrix" %in% class(m))
    expect_equal(dim(m), c(2L, 3L))

    base <- matrix(1:6, nrow = 2L, ncol = 3L)

    expect_equal(unclass(rowSums(m)), rowSums(base))
    expect_equal(unclass(colSums(m)), colSums(base))
    expect_equal(unclass(rowMeans(m)), rowMeans(base))
    expect_equal(unclass(colMeans(m)), colMeans(base))

    old_limit <- getOption("Rfmalloc.reduce_result_length")
    on.exit(options(Rfmalloc.reduce_result_length = old_limit), add = TRUE)

    options(Rfmalloc.reduce_result_length = 4L)
    expect_false(inherits(rowSums(m), "fmalloc"))
    expect_false(inherits(colSums(m), "fmalloc"))

    options(Rfmalloc.reduce_result_length = 1L)
    expect_true(inherits(rowSums(m), "fmalloc"))
    expect_true(inherits(colSums(m), "fmalloc"))
    expect_true(inherits(rowMeans(m), "fmalloc"))
    expect_true(inherits(colMeans(m), "fmalloc"))

    message("Test 3 passed")
})()

(function() {
    message("Test 4: reduction length threshold applies to larger outputs and Summary")
    tmp <- tempfile(fileext = ".bin")
    rt <- open_fmalloc(tmp)
    on.exit({
        cleanup_fmalloc(rt)
        unlink(tmp)
    }, add = TRUE)

    m <- create_fmalloc_matrix("integer", nrow = 4L, ncol = 4L, runtime = rt)
    m[] <- 1:16
    base <- matrix(1:16, nrow = 4L, ncol = 4L)

    x <- create_fmalloc_vector("integer", 4L, runtime = rt)
    x[] <- 1:4

    old_limit <- getOption("Rfmalloc.reduce_result_length")
    on.exit(options(Rfmalloc.reduce_result_length = old_limit), add = TRUE)

    options(Rfmalloc.reduce_result_length = 5L)
    expect_false(inherits(rowSums(m), "fmalloc"))
    expect_false(inherits(colSums(m), "fmalloc"))
    expect_false(inherits(rowMeans(m), "fmalloc"))
    expect_false(inherits(colMeans(m), "fmalloc"))
    expect_equal(unclass(rowSums(m)), rowSums(base))
    expect_equal(unclass(colSums(m)), colSums(base))

    options(Rfmalloc.reduce_result_length = 3L)
    before_rs <- nrow(list_fmalloc_allocations(rt))
    rs <- rowSums(m)
    cs <- colSums(m)
    rmns <- rowMeans(m)
    cmns <- colMeans(m)
    assert_fmalloc_managed(rs, rt, "fmalloc_vector", before_records = before_rs)
    assert_fmalloc_managed(cs, rt, "fmalloc_vector", before_records = before_rs)
    assert_fmalloc_managed(rmns, rt, "fmalloc_vector", before_records = before_rs)
    assert_fmalloc_managed(cmns, rt, "fmalloc_vector", before_records = before_rs)

    options(Rfmalloc.reduce_result_length = 3L)
    expect_false(inherits(range(x), "fmalloc"))

    options(Rfmalloc.reduce_result_length = 1L)
    expect_true(inherits(range(x), "fmalloc"))

    message("Test 4 passed")
})()

(function() {
    message("Test 5: matrix reduction semantics match base for logical/complex/NA edge cases")
    tmp <- tempfile(fileext = ".bin")
    rt <- open_fmalloc(tmp)
    on.exit({
        cleanup_fmalloc(rt)
        unlink(tmp)
    }, add = TRUE)

    m_int <- matrix(c(NA_integer_, NA_integer_, 1L, 2L), nrow = 2L, ncol = 2L)
    f_int <- create_fmalloc_matrix("integer", nrow = 2L, ncol = 2L, runtime = rt)
    f_int[] <- m_int

    expect_identical(rowSums(f_int), rowSums(m_int))
    expect_identical(rowSums(f_int, na.rm = TRUE), rowSums(m_int, na.rm = TRUE))
    expect_identical(colSums(f_int), colSums(m_int))
    expect_identical(colSums(f_int, na.rm = TRUE), colSums(m_int, na.rm = TRUE))
    expect_identical(rowMeans(f_int), rowMeans(m_int))
    expect_identical(rowMeans(f_int, na.rm = TRUE), rowMeans(m_int, na.rm = TRUE))
    expect_identical(colMeans(f_int), colMeans(m_int))
    expect_identical(colMeans(f_int, na.rm = TRUE), colMeans(m_int, na.rm = TRUE))

    m_empty <- matrix(numeric(0), nrow = 2L, ncol = 0L)
    f_empty <- create_fmalloc_matrix("numeric", nrow = 2L, ncol = 0L, runtime = rt)
    expect_identical(rowMeans(f_empty), rowMeans(m_empty))
    expect_identical(colMeans(f_empty), colMeans(m_empty))
    expect_identical(rowSums(f_empty), rowSums(m_empty))
    expect_identical(colMeans(f_empty, na.rm = TRUE), colMeans(m_empty, na.rm = TRUE))

    m_complex <- matrix(as.complex(c(1 + 1i, NA + NA * 1i, 2 + 0i, 3 + 0i)), nrow = 2L, ncol = 2L)
    f_complex <- create_fmalloc_matrix("complex", nrow = 2L, ncol = 2L, runtime = rt)
    f_complex[] <- m_complex
    expect_identical(rowSums(f_complex), rowSums(m_complex))
    expect_identical(rowSums(f_complex, na.rm = TRUE), rowSums(m_complex, na.rm = TRUE))
    expect_identical(colMeans(f_complex), colMeans(m_complex))
    expect_identical(colMeans(f_complex, na.rm = TRUE), colMeans(m_complex, na.rm = TRUE))

    m_logical <- matrix(c(TRUE, NA, FALSE, TRUE), nrow = 2L, ncol = 2L)
    f_logical <- create_fmalloc_matrix("logical", nrow = 2L, ncol = 2L, runtime = rt)
    f_logical[] <- m_logical
    expect_identical(rowSums(f_logical), rowSums(m_logical))
    expect_identical(rowSums(f_logical, na.rm = TRUE), rowSums(m_logical, na.rm = TRUE))
    expect_identical(colSums(f_logical), colSums(m_logical))

    v <- create_fmalloc_vector("logical", 3L, runtime = rt)
    v[] <- c(TRUE, FALSE, NA)
    expect_identical(range(v), range(c(TRUE, FALSE, NA)))
    expect_identical(range(v, na.rm = TRUE), range(c(TRUE, FALSE, NA), na.rm = TRUE))

    expect_identical(rowMeans(f_logical), rowMeans(m_logical))
    expect_identical(colMeans(f_logical, na.rm = TRUE), colMeans(m_logical, na.rm = TRUE))

    expect_error(rowSums(f_logical, dims = 2L), "invalid 'dims'")

    message("Test 5 passed")
})()

message("fmalloc dispatch tests completed")
