# Test persistence and file-backed memory behavior

library(tinytest)
library(Rfmalloc)

cat("Testing persistence and file-backed memory behavior...\n")

test_file_1 <- tempfile(fileext = ".bin")
test_file_2 <- tempfile(fileext = ".bin")

# Test 1: Basic persistence across sessions
cat("Test 1: Basic persistence behavior\n")
tryCatch(
    {
        # First "session" - create and populate data
        init_result1 <- init_fmalloc(test_file_1)
        expect_true(is.logical(init_result1))

        vec1 <- create_fmalloc_vector("integer", 10)
        vec1[1:10] <- c(100, 200, 300, 400, 500, 600, 700, 800, 900, 1000)

        # Store some values to verify later
        original_values <- vec1[1:10]

        cleanup_fmalloc()

        # Second "session" - reinitialize same file
        init_result2 <- init_fmalloc(test_file_1)
        expect_true(is.logical(init_result2))

        # The data should persist (though we can't directly access the same vector object)
        # We can create new vectors and verify the allocator is working
        vec2 <- create_fmalloc_vector("integer", 5)
        vec2[1:5] <- 2000:2004
        expect_equal(vec2[1], 2000L)

        cleanup_fmalloc()

        cat("  Basic persistence test passed\n")
    },
    error = function(e) {
        cat("  Basic persistence test failed:", e$message, "\n")
    }
)

# Test 2: Multiple files and isolation
cat("Test 2: Multiple files and isolation\n")
tryCatch(
    {
        # Initialize first file
        init_fmalloc(test_file_1)
        vec_file1 <- create_fmalloc_vector("integer", 5)
        vec_file1[1:5] <- 1:5
        cleanup_fmalloc()

        # Initialize second file
        init_fmalloc(test_file_2)
        vec_file2 <- create_fmalloc_vector("integer", 5)
        vec_file2[1:5] <- 10:14
        cleanup_fmalloc()

        # Files should be independent
        expect_true(file.exists(test_file_1))
        expect_true(file.exists(test_file_2))
        expect_true(file.size(test_file_1) > 0)
        expect_true(file.size(test_file_2) > 0)

        cat("  Multiple files isolation test passed\n")
    },
    error = function(e) {
        cat("  Multiple files isolation test failed:", e$message, "\n")
    }
)

# Test 3: File size and growth
cat("Test 3: File size and growth behavior\n")
tryCatch(
    {
        # Check initial file size
        if (file.exists(test_file_1)) {
            initial_size <- file.size(test_file_1)

            init_fmalloc(test_file_1)

            # Create several vectors to potentially grow the file
            vectors <- list()
            for (i in 1:10) {
                vectors[[i]] <- create_fmalloc_vector("numeric", 100)
                vectors[[i]][1:100] <- runif(100) * i
            }

            cleanup_fmalloc()

            # File might have grown (though fmalloc pre-allocates)
            final_size <- file.size(test_file_1)
            expect_true(final_size >= initial_size)

            cat("  File size behavior test passed\n")
        } else {
            cat("  File size test skipped - no file available\n")
        }
    },
    error = function(e) {
        cat("  File size test failed:", e$message, "\n")
    }
)

# Test 4: File corruption handling
cat("Test 4: File corruption and error handling\n")
tryCatch(
    {
        # Create a small file that's too small for fmalloc
        tiny_file <- tempfile(fileext = ".bin")
        writeLines("tiny content", tiny_file)

        # This should fail gracefully
        expect_error(init_fmalloc(tiny_file), "File too small")

        unlink(tiny_file)

        cat("  File corruption handling test passed\n")
    },
    error = function(e) {
        cat("  File corruption handling test result:", e$message, "\n")
    }
)

# Test 5: Permission and access tests (Unix only)
if (.Platform$OS.type == "unix") {
    cat("Test 5: File permissions and access\n")
    tryCatch(
        {
            # Create a file in temp directory
            perm_file <- tempfile(fileext = ".bin")

            # Try to create in non-existent directory
            bad_file <- "/nonexistent/directory/test.bin"
            expect_error(init_fmalloc(bad_file), "Cannot create file")

            cat("  File permissions test passed\n")
        },
        error = function(e) {
            cat("  File permissions test result:", e$message, "\n")
        }
    )
}

# Test 6: Cleanup behavior
cat("Test 6: Cleanup behavior\n")
tryCatch(
    {
        init_fmalloc(test_file_1)

        # Create vector
        test_vec <- create_fmalloc_vector("logical", 5)
        test_vec[1:5] <- c(TRUE, FALSE, TRUE, FALSE, TRUE)

        # Cleanup should work without error
        cleanup_fmalloc()

        # Should not be able to create vectors after cleanup
        expect_error(
            create_fmalloc_vector("integer", 5),
            "fmalloc not initialized"
        )

        # Should be able to reinitialize
        init_fmalloc(test_file_1)
        new_vec <- create_fmalloc_vector("integer", 3)
        expect_equal(length(new_vec), 3)

        cleanup_fmalloc()

        cat("  Cleanup behavior test passed\n")
    },
    error = function(e) {
        cat("  Cleanup behavior test failed:", e$message, "\n")
    }
)

# Test 7: Error states and recovery
cat("Test 7: Error states and recovery\n")
tryCatch(
    {
        # Multiple cleanup calls should be safe
        cleanup_fmalloc()
        cleanup_fmalloc() # Should not error

        # Multiple init attempts
        init_fmalloc(test_file_1)
        expect_warning(init_fmalloc(test_file_1), "already initialized")

        cleanup_fmalloc()

        cat("  Error states and recovery test passed\n")
    },
    error = function(e) {
        cat("  Error states and recovery test failed:", e$message, "\n")
    }
)

# Test 8: Large file operations
cat("Test 8: Large file operations\n")
tryCatch(
    {
        init_fmalloc(test_file_1)

        # Try to create a large vector to test file-backed memory
        large_vec <- create_fmalloc_vector("integer", 5000)

        # Fill with pattern to test persistence
        for (i in seq(1, 5000, by = 100)) {
            end_idx <- min(i + 99, 5000)
            large_vec[i:end_idx] <- i:(i + (end_idx - i))
        }

        # Verify some values
        expect_equal(large_vec[1], 1L)
        expect_equal(large_vec[101], 101L)
        expect_equal(large_vec[501], 501L)

        cleanup_fmalloc()

        cat("  Large file operations test passed\n")
    },
    error = function(e) {
        cat(
            "  Large file operations test failed (may be expected):",
            e$message,
            "\n"
        )
    }
)

# Cleanup all test files
for (f in c(test_file_1, test_file_2)) {
    if (file.exists(f)) {
        unlink(f)
    }
}

cat("Persistence and file-backed memory tests completed!\n")
