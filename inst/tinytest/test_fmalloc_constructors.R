library(tinytest)
library(Rfmalloc)

extract_payload_offset <- function(x) {
    l <- capture.output(.Internal(inspect(x)))
    l <- l[grepl("fmalloc_altrep", l)]
    if (length(l) < 1L) {
        return(NA_real_)
    }
    as.numeric(sub(".*offset=([0-9]+).*$", "\\1", l[1L]))
}

alloc_count <- function(runtime) {
    nrow(list_fmalloc_allocations(runtime))
}

message("Testing fmalloc explicit constructors and conversion helpers")

(function() {
    message("Test 1: create_fmalloc_matrix")
    tmp <- tempfile(fileext = ".bin")
    rt <- open_fmalloc(tmp)
    on.exit({
        cleanup_fmalloc(rt)
        unlink(tmp)
    }, add = TRUE)

    records_before <- alloc_count(rt)
    m <- create_fmalloc_matrix("integer", nrow = 2L, ncol = 3L, runtime = rt)
    expect_true(is.matrix(m))
    expect_equal(dim(m), c(2L, 3L))
    expect_true(length(attributes(m)) > 0L)
    expect_equal(alloc_count(rt), records_before + 1L)
    expect_true(!is.na(extract_payload_offset(m)))

    m[] <- 1:6
    expect_equal(m[1L, 1L], 1L)
    expect_equal(m[2L, 3L], 6L)

    dimnames(m) <- list(c("r1", "r2"), c("c1", "c2", "c3"))
    expect_equal(dimnames(m), list(c("r1", "r2"), c("c1", "c2", "c3")))

    expect_error(create_fmalloc_matrix("integer", nrow = 3L, runtime = rt), "nrow and ncol are required")
    expect_error(create_fmalloc_matrix("integer", nrow = -1L, ncol = 2L, runtime = rt), "nrow must be")
    expect_error(create_fmalloc_matrix("integer", nrow = 2L, ncol = -1L, runtime = rt), "must be a positive integer or zero")
    


    message("Test 1 passed")
})()

(function() {
    message("Test 2: as_fmalloc_matrix")
    tmp <- tempfile(fileext = ".bin")
    rt <- open_fmalloc(tmp)
    on.exit({
        cleanup_fmalloc(rt)
        unlink(tmp)
    }, add = TRUE)

    base_vec <- create_fmalloc_vector("integer", 6L, runtime = rt)
    base_vec[] <- 1:6
    base_payload <- extract_payload_offset(base_vec)

    records_before <- alloc_count(rt)
    m <- as_fmalloc_matrix(base_vec, ncol = 3L)
    expect_true(is.matrix(m))
    expect_equal(dim(m), c(2L, 3L))
    expect_equal(m, matrix(1:6, nrow = 2L, ncol = 3L))
    expect_equal(alloc_count(rt), records_before + 2L)
    expect_true(is.null(dim(base_vec)))
    expect_equal(base_vec[], 1:6)
    expect_true(!identical(extract_payload_offset(m), base_payload))

    m_default <- as_fmalloc_matrix(base_vec)
    expect_equal(dim(m_default), c(6L, 1L))
    expect_equal(alloc_count(rt), records_before + 4L)

    message("Test 2 passed")
})()

(function() {
    message("Test 2b: as_fmalloc_matrix(copy = FALSE) shares payload")
    tmp <- tempfile(fileext = ".bin")
    rt <- open_fmalloc(tmp)
    on.exit({
        cleanup_fmalloc(rt)
        unlink(tmp)
    }, add = TRUE)

    base_vec <- create_fmalloc_vector("integer", 6L, runtime = rt)
    base_vec[] <- 1:6
    base_payload <- extract_payload_offset(base_vec)
    records_before <- alloc_count(rt)

    m <- as_fmalloc_matrix(base_vec, ncol = 3L, copy = FALSE)
    expect_true(is.matrix(m))
    expect_equal(dim(m), c(2L, 3L))
    expect_equal(alloc_count(rt), records_before)
    expect_true(identical(extract_payload_offset(m), base_payload))
    expect_equal(as.vector(base_vec), 1:6)
    expect_equal(dim(base_vec), c(2L, 3L))
    expect_error(as_fmalloc_matrix(1:6, ncol = 3L, copy = FALSE), "fmalloc ALTREP")

    message("Test 2b passed")
})()

(function() {
    message("Test 3: create_fmalloc_array")
    tmp <- tempfile(fileext = ".bin")
    rt <- open_fmalloc(tmp)
    on.exit({
        cleanup_fmalloc(rt)
        unlink(tmp)
    }, add = TRUE)

    records_before <- alloc_count(rt)
    a <- create_fmalloc_array("numeric", dim = c(2L, 1L, 3L), runtime = rt)
    expect_true(is.array(a))
    expect_equal(dim(a), c(2L, 1L, 3L))
    expect_equal(alloc_count(rt), records_before + 1L)
    expect_true(!is.na(extract_payload_offset(a)))

    a[] <- 1:6
    expect_equal(a[2L, 1L, 3L], 6)

    message("Test 3 passed")
})()

(function() {
    message("Test 4: as_fmalloc_array")
    tmp <- tempfile(fileext = ".bin")
    rt <- open_fmalloc(tmp)
    on.exit({
        cleanup_fmalloc(rt)
        unlink(tmp)
    }, add = TRUE)

    base_vec <- create_fmalloc_vector("double", 6L, runtime = rt)
    base_vec[] <- 1:6
    base_payload <- extract_payload_offset(base_vec)

    records_before <- alloc_count(rt)
    a <- as_fmalloc_array(base_vec, dim = c(3L, 2L))
    expect_true(is.array(a))
    expect_equal(dim(a), c(3L, 2L))
    expect_equal(a[3L, 2L], 6)
    expect_equal(alloc_count(rt), records_before + 1L)
    expect_true(is.null(dim(base_vec)))
    expect_true(!identical(extract_payload_offset(a), base_payload))

    a_default <- as_fmalloc_array(base_vec)
    expect_equal(dim(a_default), c(6L))
    expect_equal(alloc_count(rt), records_before + 2L)

    message("Test 4 passed")
})()

(function() {
    message("Test 4b: as_fmalloc_array(copy = FALSE) shares payload")
    tmp <- tempfile(fileext = ".bin")
    rt <- open_fmalloc(tmp)
    on.exit({
        cleanup_fmalloc(rt)
        unlink(tmp)
    }, add = TRUE)

    base_vec <- create_fmalloc_vector("double", 6L, runtime = rt)
    base_vec[] <- 1:6
    base_payload <- extract_payload_offset(base_vec)
    records_before <- alloc_count(rt)

    a <- as_fmalloc_array(base_vec, dim = c(2L, 3L), copy = FALSE)
    expect_true(is.array(a))
    expect_equal(dim(a), c(2L, 3L))
    expect_equal(alloc_count(rt), records_before)
    expect_true(identical(extract_payload_offset(a), base_payload))
    expect_equal(a[2L, 3L], 6)
    expect_equal(dim(base_vec), c(2L, 3L))
    expect_equal(as.vector(base_vec), 1:6)

    message("Test 4b passed")
})()

(function() {
    message("Test 5: create_fmalloc_data_frame")
    tmp <- tempfile(fileext = ".bin")
    rt <- open_fmalloc(tmp)
    on.exit({
        cleanup_fmalloc(rt)
        unlink(tmp)
    }, add = TRUE)

    x <- create_fmalloc_vector("integer", 3L, runtime = rt)
    y <- create_fmalloc_vector("character", 3L, runtime = rt)
    records_before <- alloc_count(rt)
    x[] <- c(10L, 20L, 30L)
    y[] <- c("a", "b", "c")

    df <- create_fmalloc_data_frame(x = x, y = y)
    expect_equal(alloc_count(rt), records_before)
    expect_true(is.data.frame(df))
    expect_equal(colnames(df), c("x", "y"))
    expect_equal(df$x[], c(10L, 20L, 30L))
    expect_equal(df$y[], c("a", "b", "c"))
    expect_true(!is.na(extract_payload_offset(df$x)))
    expect_true(!is.na(extract_payload_offset(df$y)))

    converted_before <- alloc_count(rt)
    converted <- as_fmalloc_data_frame(df[[1]], df[[2]])
    expect_true(is.data.frame(converted))
    expect_equal(ncol(converted), 2L)
    expect_equal(alloc_count(rt), converted_before)
    expect_true(!is.na(extract_payload_offset(converted[[1]])))
    expect_true(!is.na(extract_payload_offset(converted[[2]])))
    message("Test 5 passed")
})()

message("fmalloc constructor/conversion tests completed")
