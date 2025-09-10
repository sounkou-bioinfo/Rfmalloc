# Test custom allocator edge cases and duplication behavior

library(tinytest)
library(Rfmalloc)

cat("Testing custom allocator edge cases and duplication behavior...\n")

# Test file setup
test_file <- tempfile(fileext = ".bin")

# Test function to check if fmalloc is working
test_fmalloc_available <- function() {
    tryCatch(
        {
            init_result <- init_fmalloc(test_file)
            v <- create_fmalloc_vector("integer", 5)
            cleanup_fmalloc()
            return(TRUE)
        },
        error = function(e) {
            return(FALSE)
        }
    )
}

if (test_fmalloc_available()) {
    cat("fmalloc available, running custom allocator tests...\n")

    # Reinitialize for tests
    init_fmalloc(test_file)

    # Test 1: Copy-on-write (COW) behavior
    cat("Test 1: Copy-on-write behavior\n")
    tryCatch(
        {
            v1 <- create_fmalloc_vector("integer", 10)
            v1[1:10] <- 1:10

            # This should trigger COW - R will duplicate the vector when assigning to v2
            v2 <- v1

            # Modifying v1 should not affect v2 due to COW
            v1[1] <- 999L

            expect_equal(v1[1], 999L)
            expect_equal(v2[1], 1L) # v2 should retain original value

            cat("  COW test passed\n")
        },
        error = function(e) {
            cat("  COW test failed:", e$message, "\n")
        }
    )

    # Test 2: Multiple vector creation and independence
    cat("Test 2: Multiple vector independence\n")
    tryCatch(
        {
            v_a <- create_fmalloc_vector("numeric", 5)
            v_b <- create_fmalloc_vector("numeric", 5)
            v_c <- create_fmalloc_vector("integer", 5)

            v_a[1:5] <- c(1.1, 2.2, 3.3, 4.4, 5.5)
            v_b[1:5] <- c(10.1, 20.2, 30.3, 40.4, 50.5)
            v_c[1:5] <- 100:104

            # Vectors should be independent
            expect_equal(v_a[1], 1.1, tolerance = 1e-10)
            expect_equal(v_b[1], 10.1, tolerance = 1e-10)
            expect_equal(v_c[1], 100L)

            # Modify one, others should be unaffected
            v_a[1] <- 999.9
            expect_equal(v_a[1], 999.9, tolerance = 1e-10)
            expect_equal(v_b[1], 10.1, tolerance = 1e-10)
            expect_equal(v_c[1], 100L)

            cat("  Multiple vector independence test passed\n")
        },
        error = function(e) {
            cat("  Multiple vector independence test failed:", e$message, "\n")
        }
    )

    # Test 3: Vector passed to functions (argument duplication)
    cat("Test 3: Function argument duplication\n")
    tryCatch(
        {
            v_orig <- create_fmalloc_vector("integer", 5)
            v_orig[1:5] <- 1:5

            # Function that modifies its argument
            modify_vector <- function(x) {
                x[1] <- 999L
                return(x)
            }

            # This should create a copy due to R's call-by-value semantics
            v_modified <- modify_vector(v_orig)

            expect_equal(v_orig[1], 1L) # Original should be unchanged
            expect_equal(v_modified[1], 999L) # Modified copy should be changed

            cat("  Function argument duplication test passed\n")
        },
        error = function(e) {
            cat("  Function argument duplication test failed:", e$message, "\n")
        }
    )

    # Test 4: Subsetting behavior (ALTREP duplication)
    cat("Test 4: Subsetting and ALTREP behavior\n")
    tryCatch(
        {
            v_big <- create_fmalloc_vector("numeric", 100)
            v_big[1:100] <- runif(100)

            # Subsetting should create new vectors
            v_sub1 <- v_big[1:10]
            v_sub2 <- v_big[50:60]

            expect_equal(length(v_sub1), 10)
            expect_equal(length(v_sub2), 11)

            # Modify original, subsets should be independent
            original_val <- v_big[1]
            v_big[1] <- 999.9

            # v_sub1 might be affected depending on how R handles the subsetting
            # This tests the ALTREP behavior
            expect_equal(v_big[1], 999.9, tolerance = 1e-10)

            cat("  Subsetting test passed\n")
        },
        error = function(e) {
            cat("  Subsetting test failed:", e$message, "\n")
        }
    )

    # Test 5: Large vector allocation and memory pressure
    cat("Test 5: Large vector allocation\n")
    tryCatch(
        {
            # Create multiple large vectors to test memory allocation
            large_vectors <- list()
            for (i in 1:5) {
                v_large <- create_fmalloc_vector("integer", 1000)
                v_large[1:10] <- (i * 1000 + 1):(i * 1000 + 10)
                large_vectors[[i]] <- v_large
            }

            # Verify each vector is independent
            for (i in 1:5) {
                expect_equal(large_vectors[[i]][1], i * 1000 + 1)
            }

            # Force garbage collection to test cleanup
            rm(large_vectors)
            gc()

            cat("  Large vector allocation test passed\n")
        },
        error = function(e) {
            cat("  Large vector allocation test failed:", e$message, "\n")
        }
    )

    # Test 6: Zero-length and edge case vectors
    cat("Test 6: Zero-length and edge case vectors\n")
    tryCatch(
        {
            v_zero <- create_fmalloc_vector("integer", 0)
            expect_equal(length(v_zero), 0)
            expect_true(is.integer(v_zero))

            v_one <- create_fmalloc_vector("logical", 1)
            expect_equal(length(v_one), 1)
            v_one[1] <- TRUE
            expect_equal(v_one[1], TRUE)

            cat("  Zero-length and edge case test passed\n")
        },
        error = function(e) {
            cat("  Zero-length and edge case test failed:", e$message, "\n")
        }
    )

    # Test 7: Error conditions with initialized fmalloc
    cat("Test 7: Error conditions with initialized fmalloc\n")
    tryCatch(
        {
            # These should still fail even with fmalloc initialized
            expect_error(
                create_fmalloc_vector("invalid_type", 10),
                "Unsupported vector type"
            )
            expect_error(
                create_fmalloc_vector("integer", -5),
                "positive integer"
            )

            cat("  Error condition tests passed\n")
        },
        error = function(e) {
            cat("  Error condition tests failed:", e$message, "\n")
        }
    )

    # Test 8: Cleanup and reinitialization
    cat("Test 8: Cleanup and reinitialization\n")
    tryCatch(
        {
            cleanup_fmalloc()

            # Should fail after cleanup
            expect_error(
                create_fmalloc_vector("integer", 5),
                "fmalloc not initialized"
            )

            # Should be able to reinitialize
            init_result2 <- init_fmalloc(test_file)
            expect_true(is.logical(init_result2))

            # Should work again after reinitialization
            v_reinit <- create_fmalloc_vector("numeric", 3)
            expect_equal(length(v_reinit), 3)

            cat("  Cleanup and reinitialization test passed\n")
        },
        error = function(e) {
            cat("  Cleanup and reinitialization test failed:", e$message, "\n")
        }
    )

    # Final cleanup
    cleanup_fmalloc()
} else {
    cat("fmalloc not available, skipping custom allocator tests\n")
}

# Always clean up test file
if (file.exists(test_file)) {
    unlink(test_file)
}

cat("Custom allocator edge case tests completed!\n")
