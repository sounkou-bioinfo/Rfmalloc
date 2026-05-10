# Test R's copy-on-write behavior with fmalloc ALTREP vectors

library(tinytest)
library(Rfmalloc)

message("Testing R's copy-on-write behavior with fmalloc ALTREP vectors...")

(function() {
    test_file <- tempfile(fileext = ".bin")
    on.exit({
        cleanup_fmalloc()
        unlink(test_file)
    }, add = TRUE)

    init_fmalloc(test_file)
    message("Testing COW behavior with fmalloc ALTREP vectors...")

    message("Test 1: Basic copy-on-write behavior")
    original <- create_fmalloc_vector("integer", 10)
    original[1:10] <- 1:10
    message("  Original vector created")

    reference <- original
    expect_equal(original[1], 1L)
    expect_equal(reference[1], 1L)
    expect_equal(length(original), length(reference))
    message("  Reference assignment completed")

    reference[1] <- 999L
    expect_equal(original[1], 1L)
    expect_equal(reference[1], 999L)
    message("  COW triggered successfully")

    message("Test 2: COW with function arguments")
    read_only_func <- function(vec) {
        vec[1] + vec[2]
    }
    modify_func <- function(vec) {
        vec[1] <- vec[1] + 1000L
        vec
    }
    test_vec <- create_fmalloc_vector("integer", 5)
    test_vec[1:5] <- 10:14
    result1 <- read_only_func(test_vec)
    expect_equal(test_vec[1], 10L)
    expect_equal(result1, 21L)
    result2 <- modify_func(test_vec)
    expect_equal(test_vec[1], 10L)
    expect_equal(result2[1], 1010L)
    message("  Function argument COW test passed")

    message("Test 3: COW with subsetting operations")
    big_vec <- create_fmalloc_vector("numeric", 20)
    big_vec[1:20] <- seq(1.1, 20.1, by = 1)
    subset1 <- big_vec[1:5]
    subset2 <- big_vec[10:15]
    expect_equal(length(subset1), 5)
    expect_equal(length(subset2), 6)
    expect_equal(subset1[1], 1.1, tolerance = 1e-10)
    expect_equal(subset2[1], 10.1, tolerance = 1e-10)
    subset1[1] <- 999.9
    expect_equal(big_vec[1], 1.1, tolerance = 1e-10)
    expect_equal(subset1[1], 999.9, tolerance = 1e-10)
    message("  Subsetting COW test passed")

    message("Test 4: Multiple references and COW")
    source_vec <- create_fmalloc_vector("integer", 8)
    source_vec[1:8] <- 100:107
    ref1 <- source_vec
    ref2 <- source_vec
    ref3 <- source_vec
    expect_equal(source_vec[1], 100L)
    expect_equal(ref1[1], 100L)
    expect_equal(ref2[1], 100L)
    expect_equal(ref3[1], 100L)
    ref1[1] <- 900L
    expect_equal(source_vec[1], 100L)
    expect_equal(ref1[1], 900L)
    expect_equal(ref2[1], 100L)
    expect_equal(ref3[1], 100L)
    ref2[2] <- 800L
    expect_equal(source_vec[2], 101L)
    expect_equal(ref1[2], 101L)
    expect_equal(ref2[2], 800L)
    expect_equal(ref3[2], 101L)
    message("  Multiple references COW test passed")

    message("Test 5: COW with different vector types")
    num_vec <- create_fmalloc_vector("numeric", 5)
    num_vec[1:5] <- c(1.1, 2.2, 3.3, 4.4, 5.5)
    num_copy <- num_vec
    num_copy[3] <- 999.9
    expect_equal(num_vec[3], 3.3, tolerance = 1e-10)
    expect_equal(num_copy[3], 999.9, tolerance = 1e-10)

    log_vec <- create_fmalloc_vector("logical", 4)
    log_vec[1:4] <- c(TRUE, FALSE, TRUE, FALSE)
    log_copy <- log_vec
    log_copy[2] <- TRUE
    expect_equal(log_vec[2], FALSE)
    expect_equal(log_copy[2], TRUE)
    message("  Different types COW test passed")

    message("Test 6: COW with lists containing fmalloc vectors")
    vec1 <- create_fmalloc_vector("integer", 3)
    vec2 <- create_fmalloc_vector("integer", 3)
    vec1[1:3] <- 1:3
    vec2[1:3] <- 10:12
    vec_list <- list(a = vec1, b = vec2)
    list_copy <- vec_list
    list_copy$a[1] <- 999L
    expect_equal(vec_list$a[1], 1L)
    expect_equal(list_copy$a[1], 999L)
    message("  List COW test passed")

    message("Test 7: Deep copy-on-write behavior")
    original_deep <- create_fmalloc_vector("integer", 6)
    original_deep[1:6] <- 1:6
    copy1 <- original_deep
    copy2 <- copy1
    copy3 <- copy2
    copy3[3] <- 333L
    expect_equal(original_deep[3], 3L)
    expect_equal(copy1[3], 3L)
    expect_equal(copy2[3], 3L)
    expect_equal(copy3[3], 333L)
    copy1[4] <- 444L
    expect_equal(original_deep[4], 4L)
    expect_equal(copy1[4], 444L)
    expect_equal(copy2[4], 4L)
    expect_equal(copy3[4], 4L)
    message("  Deep COW test passed")

    message("Test 8: COW with matrix and data.frame wrappers")
    matrix_fm <- create_fmalloc_vector("integer", 6)
    matrix_fm[] <- 1:6
    dim(matrix_fm) <- c(2L, 3L)
    dimnames(matrix_fm) <- list(c("r1", "r2"), c("c1", "c2", "c3"))

    tracemem(matrix_fm)
    matrix_read_events <- capture.output({
        matrix_alias <- matrix_fm
        invisible(matrix_alias[1L, 1L])
    })
    expect_equal(length(matrix_read_events), 0L)

    matrix_alias <- matrix_fm
    matrix_write_events <- capture.output({
        matrix_alias[1L, 2L] <- 99L
    })
    expect_true(length(matrix_write_events) >= 1L)
    expect_true(any(grepl("tracemem", matrix_write_events, fixed = TRUE)))
    expect_equal(matrix_fm[1L, 2L], 2L)
    expect_equal(matrix_alias[1L, 2L], 99L)
    untracemem(matrix_fm)

    df_a <- create_fmalloc_vector("integer", 3)
    df_b <- create_fmalloc_vector("integer", 3)
    df_a[] <- c(1L, 2L, 3L)
    df_b[] <- c(4L, 5L, 6L)

    df_fm <- data.frame(a = df_a, b = df_b, stringsAsFactors = FALSE)

    tracemem(df_fm)
    df_read_events <- capture.output({
        df_alias <- df_fm
        invisible(df_alias$a[1L])
    })
    expect_equal(length(df_read_events), 0L)

    df_alias <- df_fm
    df_write_events <- capture.output({
        df_alias$a[1L] <- 99L
    })
    expect_true(any(grepl("tracemem", df_write_events, fixed = TRUE)))
    expect_equal(df_fm$a[], c(1L, 2L, 3L))
    expect_equal(df_alias$a[], c(99L, 2L, 3L))
    untracemem(df_fm)

    message("  Matrix/data.frame COW test passed")

    cleanup_fmalloc()
})()

message("Copy-on-write behavior tests completed!")
