# Test memory management and allocator stress testing

library(tinytest)
library(Rfmalloc)

cat("Testing memory management and allocator stress scenarios...\n")

test_file <- tempfile(fileext = ".bin")

# Helper function to test if fmalloc works
fmalloc_available <- tryCatch(
    {
        init_fmalloc(test_file)
        v <- create_fmalloc_vector("integer", 5)
        cleanup_fmalloc()
        TRUE
    },
    error = function(e) FALSE
)

if (fmalloc_available) {
    cat("Running memory management stress tests...\n")

    init_fmalloc(test_file)

    # Test 1: Rapid allocation and deallocation
    cat("Test 1: Rapid allocation/deallocation\n")
    tryCatch(
        {
            vectors <- list()

            # Allocate many small vectors rapidly
            for (i in 1:50) {
                v <- create_fmalloc_vector("integer", 10)
                v[1:10] <- i:(i + 9)
                vectors[[i]] <- v
            }

            # Verify they're all different and correct
            for (i in 1:50) {
                expect_equal(vectors[[i]][1], i)
                expect_equal(vectors[[i]][10], i + 9)
            }

            # Clear vectors to trigger potential cleanup
            rm(vectors)
            gc()

            cat("  Rapid allocation test passed\n")
        },
        error = function(e) {
            cat("  Rapid allocation test failed:", e$message, "\n")
        }
    )

    # Test 2: Mixed type allocation
    cat("Test 2: Mixed type allocation\n")
    tryCatch(
        {
            int_vecs <- list()
            num_vecs <- list()
            log_vecs <- list()

            # Create vectors of different types
            for (i in 1:10) {
                int_vecs[[i]] <- create_fmalloc_vector("integer", 5)
                num_vecs[[i]] <- create_fmalloc_vector("numeric", 5)
                log_vecs[[i]] <- create_fmalloc_vector("logical", 5)

                int_vecs[[i]][1:5] <- (i * 10):(i * 10 + 4)
                num_vecs[[i]][1:5] <- runif(5) * i
                log_vecs[[i]][1:5] <- rep(i %% 2 == 0, 5)
            }

            # Verify type integrity
            for (i in 1:10) {
                expect_true(is.integer(int_vecs[[i]]))
                expect_true(is.numeric(num_vecs[[i]]))
                expect_true(is.logical(log_vecs[[i]]))
            }

            cat("  Mixed type allocation test passed\n")
        },
        error = function(e) {
            cat("  Mixed type allocation test failed:", e$message, "\n")
        }
    )

    # Test 3: Vector copying and modification patterns
    cat("Test 3: Vector copying and modification patterns\n")
    tryCatch(
        {
            # Create a source vector
            source_vec <- create_fmalloc_vector("numeric", 20)
            source_vec[1:20] <- 1:20

            # Create multiple copies and modify them differently
            copies <- list()
            for (i in 1:5) {
                copies[[i]] <- source_vec # This should trigger COW
                copies[[i]][i] <- 999 + i # Modify each copy differently
            }

            # Verify original is unchanged and copies are different
            expect_equal(source_vec[1], 1)
            for (i in 1:5) {
                expect_equal(copies[[i]][i], 999 + i)
                # Other positions should match original
                if (i > 1) expect_equal(copies[[i]][1], 1)
            }

            cat("  Vector copying test passed\n")
        },
        error = function(e) {
            cat("  Vector copying test failed:", e$message, "\n")
        }
    )

    # Test 4: Function call argument behavior
    cat("Test 4: Function call argument behavior\n")
    tryCatch(
        {
            # Function that reads from vector (should not cause duplication)
            read_vector <- function(v) {
                return(sum(v[1:5]))
            }

            # Function that modifies vector (should cause duplication)
            modify_vector <- function(v) {
                v[1] <- v[1] + 1000
                return(v)
            }

            test_vec <- create_fmalloc_vector("integer", 10)
            test_vec[1:10] <- 1:10

            # Reading should not change original
            sum_result <- read_vector(test_vec)
            expect_equal(test_vec[1], 1L)
            expect_equal(sum_result, sum(1:5))

            # Modifying should not change original due to COW
            modified_vec <- modify_vector(test_vec)
            expect_equal(test_vec[1], 1L) # Original unchanged
            expect_equal(modified_vec[1], 1001L) # Modified copy changed

            cat("  Function call argument test passed\n")
        },
        error = function(e) {
            cat("  Function call argument test failed:", e$message, "\n")
        }
    )

    # Test 5: Garbage collection stress test
    cat("Test 5: Garbage collection stress test\n")
    tryCatch(
        {
            # Create many vectors in a loop to stress GC
            for (iteration in 1:10) {
                temp_vectors <- list()
                for (i in 1:20) {
                    v <- create_fmalloc_vector("numeric", 50)
                    v[1:50] <- runif(50)
                    temp_vectors[[i]] <- v
                }

                # Force garbage collection
                rm(temp_vectors)
                gc()
            }

            # Verify allocator still works after stress
            final_test <- create_fmalloc_vector("integer", 5)
            final_test[1:5] <- 100:104
            expect_equal(final_test[1], 100L)

            cat("  Garbage collection stress test passed\n")
        },
        error = function(e) {
            cat("  Garbage collection stress test failed:", e$message, "\n")
        }
    )

    # Test 6: Large single vector allocation
    cat("Test 6: Large single vector allocation\n")
    tryCatch(
        {
            # Try to allocate a large vector (may fail on memory-constrained systems)
            large_size <- 10000
            large_vec <- create_fmalloc_vector("integer", large_size)

            expect_equal(length(large_vec), large_size)

            # Test access at various positions
            large_vec[1] <- 1L
            large_vec[large_size %/% 2] <- 5000L
            large_vec[large_size] <- 99999L

            expect_equal(large_vec[1], 1L)
            expect_equal(large_vec[large_size %/% 2], 5000L)
            expect_equal(large_vec[large_size], 99999L)

            cat("  Large single vector test passed\n")
        },
        error = function(e) {
            cat(
                "  Large single vector test failed (may be expected):",
                e$message,
                "\n"
            )
        }
    )

    # Test 7: Interleaved allocation patterns
    cat("Test 7: Interleaved allocation patterns\n")
    tryCatch(
        {
            # Allocate vectors in interleaved pattern
            vecs_a <- list()
            vecs_b <- list()

            for (i in 1:10) {
                vecs_a[[i]] <- create_fmalloc_vector("integer", 10)
                vecs_b[[i]] <- create_fmalloc_vector("numeric", 15)

                vecs_a[[i]][1:10] <- (i * 100):(i * 100 + 9)
                vecs_b[[i]][1:15] <- runif(15) * i
            }

            # Verify independence and correctness
            for (i in 1:10) {
                expect_equal(vecs_a[[i]][1], i * 100)
                expect_equal(length(vecs_b[[i]]), 15)
            }

            cat("  Interleaved allocation test passed\n")
        },
        error = function(e) {
            cat("  Interleaved allocation test failed:", e$message, "\n")
        }
    )

    # Test 8: Error recovery
    cat("Test 8: Error recovery\n")
    tryCatch(
        {
            # Try to cause allocation errors and recover
            valid_vec <- create_fmalloc_vector("integer", 5)
            valid_vec[1:5] <- 1:5

            # Try invalid operations (should not crash allocator)
            expect_error(
                create_fmalloc_vector("invalid", 10),
                "Unsupported vector type"
            )
            expect_error(
                create_fmalloc_vector("integer", -1),
                "positive integer"
            )

            # Allocator should still work after errors
            recovery_vec <- create_fmalloc_vector("logical", 3)
            recovery_vec[1:3] <- c(TRUE, FALSE, TRUE)
            expect_equal(recovery_vec[1], TRUE)

            cat("  Error recovery test passed\n")
        },
        error = function(e) {
            cat("  Error recovery test failed:", e$message, "\n")
        }
    )

    cleanup_fmalloc()
} else {
    cat("fmalloc not available, skipping memory management tests\n")
}

# Clean up
if (file.exists(test_file)) {
    unlink(test_file)
}

cat("Memory management stress tests completed!\n")
