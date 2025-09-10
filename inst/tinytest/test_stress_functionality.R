test_that("stress test functionality works", {
  # Test with smaller sizes for CI
  test_file <- tempfile(fileext = ".bin")
  
  # Test creating file with specific size (1 GB for testing)
  expect_silent(init_fmalloc(test_file, size_gb = 1))
  
  # Verify file was created
  expect_true(file.exists(test_file))
  
  # Check file size is approximately correct (allow some overhead)
  file_info <- file.info(test_file)
  expected_size <- 1 * 1024^3  # 1 GB in bytes
  expect_true(file_info$size >= expected_size * 0.95)  # Allow 5% variance
  
  # Test creating a moderately large vector (1M elements = ~4MB)
  big_vector <- create_fmalloc_vector("integer", 1e6)
  expect_equal(length(big_vector), 1e6)
  
  # Test basic operations on large vector
  big_vector[1:1000] <- 1:1000
  expect_equal(big_vector[1:10], 1:10)
  
  # Test with numeric vector
  big_numeric <- create_fmalloc_vector("numeric", 5e5)
  expect_equal(length(big_numeric), 5e5)
  
  big_numeric[1:100] <- (1:100) * 1.5
  expect_equal(big_numeric[1:5], (1:5) * 1.5)
  
  # Cleanup
  cleanup_fmalloc()
  file.remove(test_file)
})

test_that("size parameter validation works", {
  test_file <- tempfile(fileext = ".bin")
  
  # Test invalid size parameters
  expect_error(init_fmalloc(test_file, size_gb = -1), "size_gb must be positive")
  expect_error(init_fmalloc(test_file, size_gb = 0), "size_gb must be positive")
  expect_error(init_fmalloc(test_file, size_gb = 1001), "size_gb too large")
  expect_error(init_fmalloc(test_file, size_gb = "invalid"), "size_gb must be a single numeric value")
  
  # Test valid size
  expect_silent(init_fmalloc(test_file, size_gb = 0.1))  # 100MB
  
  # Cleanup
  cleanup_fmalloc()
  file.remove(test_file)
})

test_that("default size still works", {
  test_file <- tempfile(fileext = ".bin")
  
  # Test without size parameter (should use default)
  expect_silent(init_fmalloc(test_file))
  
  # Should create a file with default size
  expect_true(file.exists(test_file))
  file_info <- file.info(test_file)
  expect_true(file_info$size >= 32 * 1024 * 1024)  # At least 32MB
  
  # Cleanup
  cleanup_fmalloc()
  file.remove(test_file)
})
