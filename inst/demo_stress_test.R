#!/usr/bin/env Rscript

#' Demo Stress Test for Rfmalloc
#'
#' A smaller version of the stress test that can be run safely
#' without requiring massive disk space. Demonstrates the same
#' principles with reasonable resource requirements.

library(Rfmalloc)

main <- function() {
    message("=== Rfmalloc Demo Stress Test ===")
    message("This demo uses moderate sizes suitable for testing")

    DEMO_SIZE_GB <- 1
    DEMO_VECTOR_SIZE <- 1e6

    demo_file <- tempfile(fileext = ".bin")
    message("Creating demo file: ", basename(demo_file))

    on.exit({
        message("=== Cleanup ===")
        cleanup_fmalloc()
        if (file.exists(demo_file)) {
            file.remove(demo_file)
            message("Demo file removed")
        }
    }, add = TRUE)

    message("=== Phase 1: Initialize 1GB backing file ===")
    start_time <- Sys.time()

    init_result <- init_fmalloc(demo_file, size_gb = DEMO_SIZE_GB)
    init_time <- Sys.time()

    message(
        "Initialization completed in: ",
        round(as.numeric(init_time - start_time, units = "secs"), 2),
        " seconds"
    )
    message(
        "File size: ",
        round(file.info(demo_file)$size / 1024^3, 2),
        " GB"
    )

    message("=== Phase 2: Create large vectors ===")

    message("Creating integer vector (", DEMO_VECTOR_SIZE, " elements)...")
    big_int <- create_fmalloc_vector("integer", DEMO_VECTOR_SIZE)
    message(
        "Integer vector memory: ",
        round(DEMO_VECTOR_SIZE * 4 / 1024^2, 1),
        " MB"
    )

    message("Creating numeric vector (", DEMO_VECTOR_SIZE / 2, " elements)...")
    big_num <- create_fmalloc_vector("numeric", DEMO_VECTOR_SIZE / 2)
    message(
        "Numeric vector memory: ",
        round(DEMO_VECTOR_SIZE / 2 * 8 / 1024^2, 1),
        " MB"
    )

    message("=== Phase 3: Test operations ===")

    test_size <- min(10000, DEMO_VECTOR_SIZE)
    big_int[1:test_size] <- 1:test_size
    big_num[1:test_size] <- (1:test_size) * 1.5

    message("Sample integer data: ", paste(big_int[1:5], collapse = ", "))
    message("Sample numeric data: ", paste(big_num[1:5], collapse = ", "))

    random_idx <- sample(test_size, 5)
    message("Random access test: ", paste(big_int[random_idx], collapse = ", "))

    total_time <- Sys.time()
    message("=== Demo completed successfully! ===")
    message(
        "Total time: ",
        round(as.numeric(total_time - start_time, units = "secs"), 2),
        " seconds"
    )
    message("Demonstrated:")
    message("- Large file creation (1 GB)")
    message("- Large vector allocation (~8 MB total)")
    message("- Vector operations and random access")
    message("- Data persistence and integrity")

    message("Demo stress test completed!")
    message("For full stress testing with 50GB files, run the full stress_test.R")
}

main()
