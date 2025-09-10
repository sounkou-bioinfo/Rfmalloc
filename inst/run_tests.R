#!/usr/bin/env Rscript

# Test runner for fmalloc package
library(tinytest)

# Run all tests
cat("Running fmalloc package tests...\n")

# Check if package is loaded
if (!("fmalloc" %in% loadedNamespaces())) {
    stop("fmalloc package not loaded. Install and load the package first.")
}

# Run tests
test_results <- run_test_dir(system.file("tinytest", package = "fmalloc"))

# Print summary
cat("\nTest Summary:\n")
print(test_results)

# Exit with appropriate code
if (any(test_results$fail > 0, na.rm = TRUE)) {
    cat("\nSome tests failed!\n")
    quit(status = 1)
} else {
    cat("\nAll tests passed!\n")
    quit(status = 0)
}
