# Test ALTREP fmalloc edge cases and duplication behavior

library(tinytest)
library(Rfmalloc)

message("Testing ALTREP fmalloc edge cases and duplication behavior...")

(function() {
    test_file <- tempfile(fileext = ".bin")
    on.exit({
        cleanup_fmalloc()
        unlink(test_file)
    }, add = TRUE)

    init_fmalloc(test_file)
    message("fmalloc available, running ALTREP fmalloc tests...")

    message("Test 1: Copy-on-write behavior")
    v1 <- create_fmalloc_vector("integer", 10)
    v1[1:10] <- 1:10
    v2 <- v1
    v1[1] <- 999L
    expect_equal(v1[1], 999L)
    expect_equal(v2[1], 1L)
    message("  COW test passed")

    message("Test 2: Multiple vector independence")
    v_a <- create_fmalloc_vector("numeric", 5)
    v_b <- create_fmalloc_vector("numeric", 5)
    v_c <- create_fmalloc_vector("integer", 5)
    v_a[1:5] <- c(1.1, 2.2, 3.3, 4.4, 5.5)
    v_b[1:5] <- c(10.1, 20.2, 30.3, 40.4, 50.5)
    v_c[1:5] <- 100:104
    expect_equal(v_a[1], 1.1, tolerance = 1e-10)
    expect_equal(v_b[1], 10.1, tolerance = 1e-10)
    expect_equal(v_c[1], 100L)
    v_a[1] <- 999.9
    expect_equal(v_a[1], 999.9, tolerance = 1e-10)
    expect_equal(v_b[1], 10.1, tolerance = 1e-10)
    expect_equal(v_c[1], 100L)
    message("  Multiple vector independence test passed")

    message("Test 3: Function argument duplication")
    v_orig <- create_fmalloc_vector("integer", 5)
    v_orig[1:5] <- 1:5
    modify_vector <- function(x) {
        x[1] <- 999L
        x
    }
    v_modified <- modify_vector(v_orig)
    expect_equal(v_orig[1], 1L)
    expect_equal(v_modified[1], 999L)
    message("  Function argument duplication test passed")

    message("Test 4: Subsetting and ALTREP behavior")
    v_big <- create_fmalloc_vector("numeric", 100)
    v_big[1:100] <- runif(100)
    v_sub1 <- v_big[1:10]
    v_sub2 <- v_big[50:60]
    expect_equal(length(v_sub1), 10)
    expect_equal(length(v_sub2), 11)
    v_big[1] <- 999.9
    expect_equal(v_big[1], 999.9, tolerance = 1e-10)
    message("  Subsetting test passed")

    message("Test 5: Large vector allocation")
    large_vectors <- list()
    for (i in 1:5) {
        v_large <- create_fmalloc_vector("integer", 1000)
        v_large[1:10] <- (i * 1000 + 1):(i * 1000 + 10)
        large_vectors[[i]] <- v_large
    }
    for (i in 1:5) {
        expect_equal(large_vectors[[i]][1], i * 1000 + 1)
    }
    rm(large_vectors)
    gc()
    message("  Large vector allocation test passed")

    message("Test 6: Zero-length and edge case vectors")
    v_zero <- create_fmalloc_vector("integer", 0)
    expect_equal(length(v_zero), 0)
    expect_true(is.integer(v_zero))
    v_one <- create_fmalloc_vector("logical", 1)
    expect_equal(length(v_one), 1)
    v_one[1] <- TRUE
    expect_equal(v_one[1], TRUE)
    message("  Zero-length and edge case test passed")

    message("Test 7: Error conditions with initialized fmalloc")
    expect_error(
        create_fmalloc_vector("invalid_type", 10),
        "Unsupported vector type"
    )
    expect_error(
        create_fmalloc_vector("integer", -5),
        "positive integer"
    )
    message("  Error condition tests passed")

    message("Test 8: Cleanup and reinitialization")
    cleanup_fmalloc()
    expect_error(
        create_fmalloc_vector("integer", 5),
        "fmalloc not initialized"
    )
    init_result2 <- init_fmalloc(test_file)
    expect_true(is.logical(init_result2))
    v_reinit <- create_fmalloc_vector("numeric", 3)
    expect_equal(length(v_reinit), 3)
    message("  Cleanup and reinitialization test passed")
})()

message("ALTREP fmalloc edge case tests completed!")
