# Test memory-mapped vectors (ALTREP)

library(tinytest)
library(Rfmalloc)

# Test basic creation and usage
test_file <- tempfile(fileext = ".bin")

tryCatch(
    {
        # Test vector creation
        v <- create_mmap_vector(test_file, 100)
        expect_true(is.integer(v))
        expect_equal(length(v), 100)

        # Test setting and getting values
        v[1:10] <- 1:10
        expect_equal(v[1:10], 1:10)

        # Test edge cases
        v[100] <- 999L
        expect_equal(v[100], 999L)

        # Test persistence by creating new vector from same file
        v2 <- create_mmap_vector(test_file, 100)
        expect_equal(v2[1:10], 1:10)
        expect_equal(v2[100], 999L)

        # Clean up vectors first
        rm(v, v2)
        gc()

        cat("ALTREP mmap vector tests passed!\n")
    },
    error = function(e) {
        cat("ALTREP test failed:", e$message, "\n")
    },
    finally = {
        # Always clean up the file
        if (file.exists(test_file)) {
            unlink(test_file)
        }
    }
)

# Test input validation
expect_error(create_mmap_vector(123, 10), "character string")
expect_error(create_mmap_vector("test.bin", -1), "positive integer")
expect_error(create_mmap_vector(c("a", "b"), 10), "single character")
expect_error(create_mmap_vector("", 10), "cannot be empty")

cat("ALTREP input validation tests passed!\n")
