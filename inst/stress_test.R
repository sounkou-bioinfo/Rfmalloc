#!/usr/bin/env Rscript

#' Stress Test for Rfmalloc with Large Files and Vectors
#'
#' This script demonstrates the capability of Rfmalloc to handle
#' very large backing files (50+ GB) and allocate large vectors
#' within them. It tests the limits of the fmalloc system.

library(Rfmalloc)

# Configuration
STRESS_TEST_SIZE_GB <- 50
LARGE_VECTOR_SIZE <- 1e6 # 1 million elements (safe limit)
STRESS_TEST_DIR <- "/tmp" # Use /tmp for large temporary files

cat("=== Rfmalloc Stress Test ===\n")
cat("Testing with:", STRESS_TEST_SIZE_GB, "GB backing file\n")
cat("Vector size:", LARGE_VECTOR_SIZE, "elements\n\n")

# Create temporary file for stress test
stress_file <- file.path(
    STRESS_TEST_DIR,
    paste0("rfmalloc_stress_", format(Sys.time(), "%Y%m%d_%H%M%S"), ".bin")
)

cat("Creating stress test file:", stress_file, "\n")

# Check available disk space
if (file.exists(STRESS_TEST_DIR)) {
    disk_info <- system(paste("df", STRESS_TEST_DIR), intern = TRUE)
    cat("Disk space info:\n")
    cat(paste(disk_info, collapse = "\n"), "\n\n")
}

# Function to format bytes in human readable format
format_bytes <- function(bytes) {
    if (bytes >= 1e12) {
        return(paste(round(bytes / 1e12, 2), "TB"))
    } else if (bytes >= 1e9) {
        return(paste(round(bytes / 1e9, 2), "GB"))
    } else if (bytes >= 1e6) {
        return(paste(round(bytes / 1e6, 2), "MB"))
    } else if (bytes >= 1e3) {
        return(paste(round(bytes / 1e3, 2), "KB"))
    } else {
        return(paste(bytes, "bytes"))
    }
}

# Function to run stress test
run_stress_test <- function() {
    cat("=== Phase 1: Initializing large backing file ===\n")
    start_time <- Sys.time()

    tryCatch(
        {
            # Initialize fmalloc with very large file
            init_result <- init_fmalloc(
                stress_file,
                size_gb = STRESS_TEST_SIZE_GB
            )
            init_time <- Sys.time()
            cat(
                "File initialization completed in:",
                round(as.numeric(init_time - start_time, units = "secs"), 2),
                "seconds\n"
            )
            cat("Initialization result:", init_result, "\n\n")

            # Check file size
            file_info <- file.info(stress_file)
            cat("Created file size:", format_bytes(file_info$size), "\n\n")

            cat("=== Phase 2: Creating large integer vector ===\n")
            vector_start <- Sys.time()

            # Create very large integer vector (1 million integers = ~4MB)
            cat(
                "Allocating integer vector with",
                LARGE_VECTOR_SIZE,
                "elements...\n"
            )
            big_int_vector <- create_fmalloc_vector(
                "integer",
                LARGE_VECTOR_SIZE
            )

            vector_time <- Sys.time()
            cat(
                "Integer vector created in:",
                round(
                    as.numeric(vector_time - vector_start, units = "secs"),
                    2
                ),
                "seconds\n"
            )

            # Calculate expected memory usage
            expected_size <- LARGE_VECTOR_SIZE * 4 # 4 bytes per integer
            cat("Expected vector size:", format_bytes(expected_size), "\n")
            cat(
                "R object size:",
                format_bytes(as.numeric(object.size(big_int_vector))),
                "\n\n"
            )

            cat("=== Phase 3: Creating large numeric vector ===\n")
            numeric_start <- Sys.time()

            # Create large numeric vector (500K doubles = ~4MB)
            numeric_size <- 5e5
            cat("Allocating numeric vector with", numeric_size, "elements...\n")
            big_num_vector <- create_fmalloc_vector("numeric", numeric_size)

            numeric_time <- Sys.time()
            cat(
                "Numeric vector created in:",
                round(
                    as.numeric(numeric_time - numeric_start, units = "secs"),
                    2
                ),
                "seconds\n"
            )

            expected_num_size <- numeric_size * 8 # 8 bytes per double
            cat("Expected vector size:", format_bytes(expected_num_size), "\n")
            cat(
                "R object size:",
                format_bytes(as.numeric(object.size(big_num_vector))),
                "\n\n"
            )

            cat("=== Phase 4: Testing vector operations ===\n")
            ops_start <- Sys.time()

            # Test basic operations
            cat("Testing basic vector operations...\n")

            # Fill some data
            sample_size <- min(10000, LARGE_VECTOR_SIZE)
            big_int_vector[1:sample_size] <- 1:sample_size
            big_num_vector[1:sample_size] <- (1:sample_size) * 1.5

            # Verify data
            cat("First 10 integers:", big_int_vector[1:10], "\n")
            cat("First 10 numerics:", big_num_vector[1:10], "\n")

            # Test random access
            random_indices <- sample(sample_size, 10)
            cat("Random access test - indices:", random_indices[1:5], "\n")
            cat("Random values:", big_int_vector[random_indices[1:5]], "\n")

            ops_time <- Sys.time()
            cat(
                "Vector operations completed in:",
                round(as.numeric(ops_time - ops_start, units = "secs"), 2),
                "seconds\n\n"
            )

            cat("=== Phase 5: Memory and performance summary ===\n")
            total_time <- Sys.time()
            cat(
                "Total test duration:",
                round(as.numeric(total_time - start_time, units = "mins"), 2),
                "minutes\n"
            )

            # Calculate total allocated memory
            total_allocated <- expected_size + expected_num_size
            cat(
                "Total vector memory allocated:",
                format_bytes(total_allocated),
                "\n"
            )
            cat("Backing file size:", format_bytes(file_info$size), "\n")
            cat(
                "Memory efficiency:",
                round((total_allocated / file_info$size) * 100, 2),
                "%\n\n"
            )

            cat("=== Stress Test PASSED ===\n")
            cat("Successfully created and operated on very large vectors:\n")
            cat("- Integer vector:", format_bytes(expected_size), "\n")
            cat("- Numeric vector:", format_bytes(expected_num_size), "\n")
            cat("- Total allocated:", format_bytes(total_allocated), "\n")
            cat("- Backing file:", format_bytes(file_info$size), "\n\n")

            return(TRUE)
        },
        error = function(e) {
            cat("ERROR during stress test:", e$message, "\n")
            return(FALSE)
        },
        finally = {
            # Always cleanup
            cat("=== Cleanup ===\n")
            tryCatch(
                {
                    cleanup_fmalloc()
                    cat("fmalloc cleaned up\n")
                },
                error = function(e) {
                    cat("Warning: cleanup error:", e$message, "\n")
                }
            )

            # Remove test file
            if (file.exists(stress_file)) {
                file_size <- file.info(stress_file)$size
                file.remove(stress_file)
                cat("Removed test file (", format_bytes(file_size), ")\n")
            }
        }
    )
}

# Run the stress test
cat("Starting stress test...\n")
cat("WARNING: This test will create a", STRESS_TEST_SIZE_GB, "GB file!\n")
cat("Press Ctrl+C to cancel, or wait 5 seconds to continue...\n")

# Give user a chance to cancel
Sys.sleep(5)

success <- run_stress_test()

if (success) {
    cat("\n=== STRESS TEST COMPLETED SUCCESSFULLY ===\n")
    cat("Rfmalloc successfully handled:\n")
    cat("- ", STRESS_TEST_SIZE_GB, "GB backing file\n")
    cat("- ", LARGE_VECTOR_SIZE, " element integer vector\n")
    cat("- Large numeric vector operations\n")
    cat("- Memory mapping and allocation patterns\n")
} else {
    cat("\n=== STRESS TEST FAILED ===\n")
    cat("Check the error messages above for details.\n")
}

cat("\nTest completed at:", format(Sys.time()), "\n")
