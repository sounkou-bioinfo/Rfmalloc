# Test persistence and file-backed memory behavior

library(tinytest)
library(Rfmalloc)

message("Testing persistence and file-backed memory behavior...")

(function() {
    message("Test 1: Basic persistence behavior")
    test_file <- tempfile(fileext = ".bin")
    on.exit({
        cleanup_fmalloc()
        unlink(test_file)
    }, add = TRUE)

    init_result1 <- init_fmalloc(test_file)
    expect_true(is.logical(init_result1))

    vec1 <- create_fmalloc_vector("integer", 10)
    vec1[1:10] <- c(100, 200, 300, 400, 500, 600, 700, 800, 900, 1000)
    blob <- serialize(vec1, NULL)
    cleanup_fmalloc()

    recovered <- unserialize(blob)
    expect_equal(recovered[1:3], c(100L, 200L, 300L))

    rm(vec1, recovered)
    gc()

    init_result2 <- init_fmalloc(test_file)
    expect_true(is.logical(init_result2))

    vec2 <- create_fmalloc_vector("integer", 5)
    vec2[1:5] <- 2000:2004
    expect_equal(vec2[1], 2000L)

    cleanup_fmalloc()
    message("  Basic persistence test passed")
})()

(function() {
    message("Test 2: Multiple files and isolation")
    test_file_1 <- tempfile(fileext = ".bin")
    test_file_2 <- tempfile(fileext = ".bin")
    on.exit({
        cleanup_fmalloc()
        unlink(c(test_file_1, test_file_2))
    }, add = TRUE)

    init_fmalloc(test_file_1)
    vec_file1 <- create_fmalloc_vector("integer", 5)
    vec_file1[1:5] <- 1:5
    cleanup_fmalloc()

    init_fmalloc(test_file_2)
    vec_file2 <- create_fmalloc_vector("integer", 5)
    vec_file2[1:5] <- 10:14
    cleanup_fmalloc()

    expect_true(file.exists(test_file_1))
    expect_true(file.exists(test_file_2))
    expect_true(file.size(test_file_1) > 0)
    expect_true(file.size(test_file_2) > 0)

    message("  Multiple files isolation test passed")
})()

(function() {
    message("Test 3: File size and growth behavior")
    test_file <- tempfile(fileext = ".bin")
    on.exit({
        cleanup_fmalloc()
        unlink(test_file)
    }, add = TRUE)

    init_fmalloc(test_file)
    initial_size <- file.size(test_file)

    vectors <- list()
    for (i in 1:10) {
        vectors[[i]] <- create_fmalloc_vector("numeric", 100)
        vectors[[i]][1:100] <- runif(100) * i
    }

    cleanup_fmalloc()
    final_size <- file.size(test_file)
    expect_true(final_size >= initial_size)

    message("  File size behavior test passed")
})()

(function() {
    message("Test 4: File corruption and error handling")
    tiny_file <- tempfile(fileext = ".bin")
    on.exit(unlink(tiny_file), add = TRUE)
    writeLines("tiny content", tiny_file)

    expect_error(init_fmalloc(tiny_file), "File too small")
    message("  File corruption handling test passed")
})()

if (.Platform$OS.type == "unix") {
    (function() {
        message("Test 5: File permissions and access")
        bad_file <- "/nonexistent/directory/test.bin"
        expect_error(init_fmalloc(bad_file), "Cannot create file")
        message("  File permissions test passed")
    })()
}

(function() {
    message("Test 6: Cleanup behavior")
    test_file <- tempfile(fileext = ".bin")
    on.exit({
        cleanup_fmalloc()
        unlink(test_file)
    }, add = TRUE)

    init_fmalloc(test_file)
    test_vec <- create_fmalloc_vector("logical", 5)
    test_vec[1:5] <- c(TRUE, FALSE, TRUE, FALSE, TRUE)
    cleanup_fmalloc()

    expect_error(
        create_fmalloc_vector("integer", 5),
        "fmalloc not initialized"
    )

    init_fmalloc(test_file)
    new_vec <- create_fmalloc_vector("integer", 3)
    expect_equal(length(new_vec), 3)

    cleanup_fmalloc()
    message("  Cleanup behavior test passed")
})()

(function() {
    message("Test 7: Error states and recovery")
    test_file <- tempfile(fileext = ".bin")
    on.exit({
        cleanup_fmalloc()
        unlink(test_file)
    }, add = TRUE)

    cleanup_fmalloc()
    cleanup_fmalloc()

    init_fmalloc(test_file)
    expect_warning(init_fmalloc(test_file), "already initialized")

    cleanup_fmalloc()
    message("  Error states and recovery test passed")
})()

(function() {
    message("Test 8: Large file operations")
    test_file <- tempfile(fileext = ".bin")
    on.exit({
        cleanup_fmalloc()
        unlink(test_file)
    }, add = TRUE)

    init_fmalloc(test_file)
    large_vec <- create_fmalloc_vector("integer", 5000)

    for (i in seq(1, 5000, by = 100)) {
        end_idx <- min(i + 99, 5000)
        large_vec[i:end_idx] <- i:(i + (end_idx - i))
    }

    expect_equal(large_vec[1], 1L)
    expect_equal(large_vec[101], 101L)
    expect_equal(large_vec[501], 501L)

    cleanup_fmalloc()
    message("  Large file operations test passed")
})()

(function() {
    message("Test 9: Persistent allocation catalog")
    test_file <- tempfile(fileext = ".bin")
    on.exit({
        cleanup_fmalloc()
        unlink(test_file)
    }, add = TRUE)

    rt <- open_fmalloc(test_file, mode = "persistent")
    expect_equal(nrow(list_fmalloc_allocations(rt)), 0L)

    int_vec <- create_fmalloc_vector("integer", 4, runtime = rt)
    int_vec[] <- 1:4
    chr_vec <- create_fmalloc_vector("character", 3, runtime = rt)
    chr_vec[] <- c("one", NA_character_, "three")
    list_child <- create_fmalloc_vector("integer", 2, runtime = rt)
    list_child[] <- 1:2
    list_vec <- create_fmalloc_vector("list", 2, runtime = rt)
    list_vec[[1]] <- list_child
    expect_error(list_vec[[2]] <- 1:2, "ordinary R objects")

    catalog <- list_fmalloc_allocations(rt)
    expect_true(nrow(catalog) >= 3L)
    expect_true(all(c(
        "record_offset", "generation", "state", "type", "length",
        "payload_offset", "payload_nbytes", "flags", "recoverable"
    ) %in% names(catalog)))
    expect_true(all(c("integer", "character", "list") %in% catalog$type))
    expect_true(all(catalog$state == "committed"))
    expect_true(any(catalog$type == "list" & !catalog$recoverable))
    expect_true(all(catalog$record_offset > 0))
    expect_equal(length(unique(catalog$generation)), nrow(catalog))

    int_blob <- serialize(int_vec, NULL)
    chr_blob <- serialize(chr_vec, NULL)
    list_blob <- serialize(list_vec, NULL)
    cleanup_fmalloc(rt)

    int_recovered <- unserialize(int_blob)
    chr_recovered <- unserialize(chr_blob)
    list_recovered <- unserialize(list_blob)
    expect_equal(int_recovered[], 1:4)
    expect_equal(chr_recovered[], c("one", NA_character_, "three"))
    expect_true(is.list(list_recovered))
    expect_equal(list_recovered[[1]][], 1:2)
    expect_equal(list_recovered[[2]], NULL)

    message("  Persistent allocation catalog test passed")
})()

message("Persistence and file-backed memory tests completed!")
