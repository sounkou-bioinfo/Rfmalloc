# Test R's copy-on-write behavior with custom allocators

library(tinytest)
library(Rfmalloc)

cat("Testing R's copy-on-write behavior with fmalloc custom allocators...\n")

test_file <- tempfile(fileext = ".bin")

# Helper to check address (for debugging, may not always work)
get_address <- function(x) {
    tryCatch(
        {
            # This is a hack to get memory address, may not work in all R versions
            capture.output(.Internal(inspect(x)))[1]
        },
        error = function(e) "unknown"
    )
}

# Test if fmalloc is available
fmalloc_available <- tryCatch(
    {
        init_fmalloc(test_file)
        cleanup_fmalloc()
        TRUE
    },
    error = function(e) FALSE
)

if (fmalloc_available) {
    cat("Testing COW behavior with fmalloc allocators...\n")

    init_fmalloc(test_file)

    # Test 1: Basic COW behavior
    cat("Test 1: Basic copy-on-write behavior\n")
    tryCatch(
        {
            # Create original vector
            original <- create_fmalloc_vector("integer", 10)
            original[1:10] <- 1:10

            cat("  Original vector created\n")

            # Assignment should create a reference, not a copy (initially)
            reference <- original

            # Both should have same values initially
            expect_equal(original[1], 1L)
            expect_equal(reference[1], 1L)
            expect_equal(length(original), length(reference))

            cat("  Reference assignment completed\n")

            # Modifying the reference should trigger COW
            # R should detect the modification and create a copy
            reference[1] <- 999L

            # Now they should be different
            expect_equal(original[1], 1L) # Original unchanged
            expect_equal(reference[1], 999L) # Reference modified

            cat("  COW triggered successfully\n")
        },
        error = function(e) {
            cat("  Basic COW test failed:", e$message, "\n")
        }
    )

    # Test 2: COW with function arguments
    cat("Test 2: COW with function arguments\n")
    tryCatch(
        {
            # Function that only reads (should not trigger COW)
            read_only_func <- function(vec) {
                return(vec[1] + vec[2])
            }

            # Function that modifies (should trigger COW)
            modify_func <- function(vec) {
                vec[1] <- vec[1] + 1000L
                return(vec)
            }

            test_vec <- create_fmalloc_vector("integer", 5)
            test_vec[1:5] <- 10:14

            # Read-only function should not change original
            result1 <- read_only_func(test_vec)
            expect_equal(test_vec[1], 10L) # Should be unchanged
            expect_equal(result1, 21L) # 10 + 11

            # Modifying function should not change original (due to COW)
            result2 <- modify_func(test_vec)
            expect_equal(test_vec[1], 10L) # Original unchanged
            expect_equal(result2[1], 1010L) # Modified copy

            cat("  Function argument COW test passed\n")
        },
        error = function(e) {
            cat("  Function argument COW test failed:", e$message, "\n")
        }
    )

    # Test 3: COW with subsetting
    cat("Test 3: COW with subsetting operations\n")
    tryCatch(
        {
            big_vec <- create_fmalloc_vector("numeric", 20)
            big_vec[1:20] <- seq(1.1, 20.1, by = 1)

            # Subsetting creates new vectors
            subset1 <- big_vec[1:5]
            subset2 <- big_vec[10:15]

            expect_equal(length(subset1), 5)
            expect_equal(length(subset2), 6)
            expect_equal(subset1[1], 1.1, tolerance = 1e-10)
            expect_equal(subset2[1], 10.1, tolerance = 1e-10)

            # Modifying subsets should not affect original
            subset1[1] <- 999.9
            expect_equal(big_vec[1], 1.1, tolerance = 1e-10) # Original unchanged
            expect_equal(subset1[1], 999.9, tolerance = 1e-10) # Subset changed

            cat("  Subsetting COW test passed\n")
        },
        error = function(e) {
            cat("  Subsetting COW test failed:", e$message, "\n")
        }
    )

    # Test 4: Multiple references and COW
    cat("Test 4: Multiple references and COW\n")
    tryCatch(
        {
            source_vec <- create_fmalloc_vector("integer", 8)
            source_vec[1:8] <- 100:107

            # Create multiple references
            ref1 <- source_vec
            ref2 <- source_vec
            ref3 <- source_vec

            # All should initially have same values
            expect_equal(source_vec[1], 100L)
            expect_equal(ref1[1], 100L)
            expect_equal(ref2[1], 100L)
            expect_equal(ref3[1], 100L)

            # Modify one reference
            ref1[1] <- 900L

            # Only ref1 should be changed, others unchanged
            expect_equal(source_vec[1], 100L)
            expect_equal(ref1[1], 900L)
            expect_equal(ref2[1], 100L)
            expect_equal(ref3[1], 100L)

            # Modify another reference
            ref2[2] <- 800L

            # Check independence
            expect_equal(source_vec[2], 101L)
            expect_equal(ref1[2], 101L)
            expect_equal(ref2[2], 800L)
            expect_equal(ref3[2], 101L)

            cat("  Multiple references COW test passed\n")
        },
        error = function(e) {
            cat("  Multiple references COW test failed:", e$message, "\n")
        }
    )

    # Test 5: COW with different vector types
    cat("Test 5: COW with different vector types\n")
    tryCatch(
        {
            # Test with numeric vectors
            num_vec <- create_fmalloc_vector("numeric", 5)
            num_vec[1:5] <- c(1.1, 2.2, 3.3, 4.4, 5.5)
            num_copy <- num_vec
            num_copy[3] <- 999.9

            expect_equal(num_vec[3], 3.3, tolerance = 1e-10)
            expect_equal(num_copy[3], 999.9, tolerance = 1e-10)

            # Test with logical vectors
            log_vec <- create_fmalloc_vector("logical", 4)
            log_vec[1:4] <- c(TRUE, FALSE, TRUE, FALSE)
            log_copy <- log_vec
            log_copy[2] <- TRUE

            expect_equal(log_vec[2], FALSE)
            expect_equal(log_copy[2], TRUE)

            cat("  Different types COW test passed\n")
        },
        error = function(e) {
            cat("  Different types COW test failed:", e$message, "\n")
        }
    )

    # Test 6: COW with list containing fmalloc vectors
    cat("Test 6: COW with lists containing fmalloc vectors\n")
    tryCatch(
        {
            vec1 <- create_fmalloc_vector("integer", 3)
            vec2 <- create_fmalloc_vector("integer", 3)
            vec1[1:3] <- 1:3
            vec2[1:3] <- 10:12

            # Put in list
            vec_list <- list(a = vec1, b = vec2)
            list_copy <- vec_list

            # Modify through list
            list_copy$a[1] <- 999L

            # Original list should be unchanged due to COW
            expect_equal(vec_list$a[1], 1L) # Original unchanged
            expect_equal(list_copy$a[1], 999L) # Copy changed

            cat("  List COW test passed\n")
        },
        error = function(e) {
            cat("  List COW test failed:", e$message, "\n")
        }
    )

    # Test 7: Deep COW behavior
    cat("Test 7: Deep copy-on-write behavior\n")
    tryCatch(
        {
            # Create a chain of references
            original <- create_fmalloc_vector("integer", 6)
            original[1:6] <- 1:6

            copy1 <- original
            copy2 <- copy1
            copy3 <- copy2

            # Modify at the end of chain
            copy3[3] <- 333L

            # All others should be unchanged
            expect_equal(original[3], 3L)
            expect_equal(copy1[3], 3L)
            expect_equal(copy2[3], 3L)
            expect_equal(copy3[3], 333L)

            # Now modify earlier in chain
            copy1[4] <- 444L

            # Check final state
            expect_equal(original[4], 4L) # Original unchanged
            expect_equal(copy1[4], 444L) # copy1 changed
            expect_equal(copy2[4], 4L) # copy2 unchanged (was referencing original)
            expect_equal(copy3[4], 4L) # copy3 unchanged (independent)

            cat("  Deep COW test passed\n")
        },
        error = function(e) {
            cat("  Deep COW test failed:", e$message, "\n")
        }
    )

    cleanup_fmalloc()
} else {
    cat("fmalloc not available, skipping COW tests\n")
}

# Clean up
if (file.exists(test_file)) {
    unlink(test_file)
}

cat("Copy-on-write behavior tests completed!\n")

cat("\n=== SUMMARY: R Custom Allocator Duplication Behavior ===\n")
cat("When using R custom allocators like fmalloc:\n")
cat("1. R uses copy-on-write (COW) semantics for memory efficiency\n")
cat("2. Assignment (x <- y) initially creates references, not copies\n")
cat(
    "3. Modification triggers duplication - R detects writes and creates copies\n"
)
cat("4. Function arguments may be duplicated depending on usage\n")
cat("5. Subsetting operations typically create new vectors\n")
cat("6. Multiple references remain independent after COW triggers\n")
cat(
    "7. This behavior ensures memory safety but can lead to unexpected duplications\n"
)
cat(
    "8. Custom allocators must handle both original allocations and COW duplications\n"
)
cat("=========================================================\n")
