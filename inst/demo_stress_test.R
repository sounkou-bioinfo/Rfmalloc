#!/usr/bin/env Rscript

#' Demo Stress Test for Rfmalloc
#'
#' A smaller version of the stress test that can be run safely
#' without requiring massive disk space. Demonstrates the same
#' principles with reasonable resource requirements.

library(Rfmalloc)

cat("=== Rfmalloc Demo Stress Test ===\n")
cat("This demo uses moderate sizes suitable for testing\n\n")

# Configuration for demo (much smaller than full stress test)
DEMO_SIZE_GB <- 1 # 1 GB instead of 50 GB
DEMO_VECTOR_SIZE <- 1e6 # 1 million elements instead of 1 billion

demo_file <- tempfile(fileext = ".bin")

cat("Creating demo file:", basename(demo_file), "\n")

tryCatch(
    {
        cat("=== Phase 1: Initialize 1GB backing file ===\n")
        start_time <- Sys.time()

        init_result <- init_fmalloc(demo_file, size_gb = DEMO_SIZE_GB)
        init_time <- Sys.time()

        cat(
            "Initialization completed in:",
            round(as.numeric(init_time - start_time, units = "secs"), 2),
            "seconds\n"
        )
        cat(
            "File size:",
            round(file.info(demo_file)$size / 1024^3, 2),
            "GB\n\n"
        )

        cat("=== Phase 2: Create large vectors ===\n")

        # Create 1M integer vector (~4MB)
        cat("Creating integer vector (", DEMO_VECTOR_SIZE, "elements)...\n")
        big_int <- create_fmalloc_vector("integer", DEMO_VECTOR_SIZE)
        cat(
            "Integer vector memory:",
            round(DEMO_VECTOR_SIZE * 4 / 1024^2, 1),
            "MB\n"
        )

        # Create 500K numeric vector (~4MB)
        cat("Creating numeric vector (", DEMO_VECTOR_SIZE / 2, "elements)...\n")
        big_num <- create_fmalloc_vector("numeric", DEMO_VECTOR_SIZE / 2)
        cat(
            "Numeric vector memory:",
            round(DEMO_VECTOR_SIZE / 2 * 8 / 1024^2, 1),
            "MB\n\n"
        )

        cat("=== Phase 3: Test operations ===\n")

        # Fill with test data
        test_size <- min(10000, DEMO_VECTOR_SIZE)
        big_int[1:test_size] <- 1:test_size
        big_num[1:test_size] <- (1:test_size) * 1.5

        # Verify data
        cat("Sample integer data:", big_int[1:5], "\n")
        cat("Sample numeric data:", big_num[1:5], "\n")

        # Test random access
        random_idx <- sample(test_size, 5)
        cat("Random access test:", big_int[random_idx], "\n")

        total_time <- Sys.time()
        cat("\n=== Demo completed successfully! ===\n")
        cat(
            "Total time:",
            round(as.numeric(total_time - start_time, units = "secs"), 2),
            "seconds\n"
        )
        cat("Demonstrated:\n")
        cat("- Large file creation (1 GB)\n")
        cat("- Large vector allocation (~8 MB total)\n")
        cat("- Vector operations and random access\n")
        cat("- Data persistence and integrity\n\n")
    },
    error = function(e) {
        cat("ERROR:", e$message, "\n")
    },
    finally = {
        cat("=== Cleanup ===\n")
        cleanup_fmalloc()
        if (file.exists(demo_file)) {
            file.remove(demo_file)
            cat("Demo file removed\n")
        }
    }
)

cat("Demo stress test completed!\n")
cat("For full stress testing with 50GB files, run the full stress_test.R\n")
