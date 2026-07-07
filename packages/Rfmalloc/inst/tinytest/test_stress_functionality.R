library(tinytest)
library(Rfmalloc)

message("Testing stress functionality helpers...")

(function() {
  test_file <- tempfile(fileext = ".bin")
  on.exit({
    cleanup_fmalloc()
    unlink(test_file)
  }, add = TRUE)

  expect_silent(init_fmalloc(test_file, size_gb = 1))
  expect_true(file.exists(test_file))

  file_info <- file.info(test_file)
  expected_size <- 1 * 1024^3
  expect_true(file_info$size >= expected_size * 0.95)

  big_vector <- create_fmalloc_vector("integer", 1e6)
  expect_equal(length(big_vector), 1e6)
  big_vector[1:1000] <- 1:1000
  expect_equal(big_vector[1:10], 1:10)

  big_numeric <- create_fmalloc_vector("numeric", 5e5)
  expect_equal(length(big_numeric), 5e5)
  big_numeric[1:100] <- (1:100) * 1.5
  expect_equal(big_numeric[1:5], (1:5) * 1.5)
})()

message("Testing size parameter validation...")
(function() {
  test_file <- tempfile(fileext = ".bin")
  on.exit({
    cleanup_fmalloc()
    unlink(test_file)
  }, add = TRUE)

  expect_error(init_fmalloc(test_file, size_gb = -1), "size_gb must be a positive numeric value")
  expect_error(init_fmalloc(test_file, size_gb = 0), "size_gb must be a positive numeric value")
  expect_error(init_fmalloc(test_file, size_gb = 1001), "size_gb too large")
  expect_error(
    init_fmalloc(test_file, size_gb = "invalid"),
    "size_gb must be a positive numeric value"
  )

  expect_silent(init_fmalloc(test_file, size_gb = 0.1))
})()

message("Testing default size...")
(function() {
  test_file <- tempfile(fileext = ".bin")
  on.exit({
    cleanup_fmalloc()
    unlink(test_file)
  }, add = TRUE)

  expect_silent(init_fmalloc(test_file))
  expect_true(file.exists(test_file))
  file_info <- file.info(test_file)
  expect_true(file_info$size >= 32 * 1024 * 1024)

  default_big <- create_fmalloc_vector("integer", 5e6)
  expect_equal(length(default_big), 5e6)
  default_big[1:5] <- 1:5
  expect_equal(default_big[1:5], 1:5)
})()

message("Testing mixed backing-file chunk geometries...")
(function() {
  file_a <- tempfile(fileext = ".bin")
  file_b <- tempfile(fileext = ".bin")
  on.exit({
    cleanup_fmalloc()
    unlink(c(file_a, file_b))
  }, add = TRUE)

  expect_silent(init_fmalloc(file_a))
  vec_a <- create_fmalloc_vector("integer", 100)
  vec_a[1] <- 11L
  cleanup_fmalloc()

  expect_silent(init_fmalloc(file_b, size_gb = 0.2))
  vec_b <- create_fmalloc_vector("numeric", 3e6)
  vec_b[1] <- 22

  rm(vec_a)
  gc()

  vec_b2 <- create_fmalloc_vector("integer", 100)
  vec_b2[1] <- 33L
  expect_equal(vec_b[1], 22)
  expect_equal(vec_b2[1], 33L)
})()

message("Stress functionality tests completed!")
