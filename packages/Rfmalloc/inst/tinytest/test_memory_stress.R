# Test memory management and allocator stress testing

library(tinytest)
library(Rfmalloc)

message("Testing memory management and allocator stress scenarios...")

(function() {
    test_file <- tempfile(fileext = ".bin")
    on.exit({
        cleanup_fmalloc()
        unlink(test_file)
    }, add = TRUE)

    init_fmalloc(test_file)
    message("Running memory management stress tests...")

    message("Test 1: Rapid allocation/deallocation")
    vectors <- list()
    for (i in 1:50) {
        v <- create_fmalloc_vector("integer", 10)
        v[1:10] <- i:(i + 9)
        vectors[[i]] <- v
    }
    for (i in 1:50) {
        expect_equal(vectors[[i]][1], i)
        expect_equal(vectors[[i]][10], i + 9)
    }
    rm(vectors)
    gc()
    message("  Rapid allocation test passed")

    message("Test 2: Mixed type allocation")
    int_vecs <- list()
    num_vecs <- list()
    log_vecs <- list()
    for (i in 1:10) {
        int_vecs[[i]] <- create_fmalloc_vector("integer", 5)
        num_vecs[[i]] <- create_fmalloc_vector("numeric", 5)
        log_vecs[[i]] <- create_fmalloc_vector("logical", 5)

        int_vecs[[i]][1:5] <- (i * 10):(i * 10 + 4)
        num_vecs[[i]][1:5] <- runif(5) * i
        log_vecs[[i]][1:5] <- rep(i %% 2 == 0, 5)
    }
    for (i in 1:10) {
        expect_true(is.integer(int_vecs[[i]]))
        expect_true(is.numeric(num_vecs[[i]]))
        expect_true(is.logical(log_vecs[[i]]))
    }
    message("  Mixed type allocation test passed")

    message("Test 3: Vector copying and modification patterns")
    source_vec <- create_fmalloc_vector("numeric", 20)
    source_vec[1:20] <- 1:20
    copies <- list()
    for (i in 1:5) {
        copies[[i]] <- source_vec
        copies[[i]][i] <- 999 + i
    }
    expect_equal(source_vec[1], 1)
    for (i in 1:5) {
        expect_equal(copies[[i]][i], 999 + i)
        if (i > 1) expect_equal(copies[[i]][1], 1)
    }
    message("  Vector copying test passed")

    message("Test 4: Function call argument behavior")
    read_vector <- function(v) {
        sum(v[1:5])
    }
    modify_vector <- function(v) {
        v[1] <- v[1] + 1000
        v
    }
    test_vec <- create_fmalloc_vector("integer", 10)
    test_vec[1:10] <- 1:10
    sum_result <- read_vector(test_vec)
    expect_equal(test_vec[1], 1L)
    expect_equal(sum_result, sum(1:5))
    modified_vec <- modify_vector(test_vec)
    expect_equal(test_vec[1], 1L)
    expect_equal(modified_vec[1], 1001L)
    message("  Function call argument test passed")

    message("Test 5: Garbage collection stress test")
    for (iteration in 1:10) {
        temp_vectors <- list()
        for (i in 1:20) {
            v <- create_fmalloc_vector("numeric", 50)
            v[1:50] <- runif(50)
            temp_vectors[[i]] <- v
        }
        rm(temp_vectors)
        gc()
    }
    final_test <- create_fmalloc_vector("integer", 5)
    final_test[1:5] <- 100:104
    expect_equal(final_test[1], 100L)
    message("  Garbage collection stress test passed")

    message("Test 6: Large single vector allocation")
    large_size <- 10000
    large_vec <- create_fmalloc_vector("integer", large_size)
    expect_equal(length(large_vec), large_size)
    large_vec[1] <- 1L
    large_vec[large_size %/% 2] <- 5000L
    large_vec[large_size] <- 99999L
    expect_equal(large_vec[1], 1L)
    expect_equal(large_vec[large_size %/% 2], 5000L)
    expect_equal(large_vec[large_size], 99999L)
    message("  Large single vector test passed")

    message("Test 7: Interleaved allocation patterns")
    vecs_a <- list()
    vecs_b <- list()
    for (i in 1:10) {
        vecs_a[[i]] <- create_fmalloc_vector("integer", 10)
        vecs_b[[i]] <- create_fmalloc_vector("numeric", 15)

        vecs_a[[i]][1:10] <- (i * 100):(i * 100 + 9)
        vecs_b[[i]][1:15] <- runif(15) * i
    }
    for (i in 1:10) {
        expect_equal(vecs_a[[i]][1], i * 100)
        expect_equal(length(vecs_b[[i]]), 15)
    }
    message("  Interleaved allocation test passed")

    message("Test 8: Error recovery")
    valid_vec <- create_fmalloc_vector("integer", 5)
    valid_vec[1:5] <- 1:5
    expect_error(
        create_fmalloc_vector("invalid", 10),
        "Unsupported vector type"
    )
    expect_error(
        create_fmalloc_vector("integer", -1),
        "non-negative"
    )
    recovery_vec <- create_fmalloc_vector("logical", 3)
    recovery_vec[1:3] <- c(TRUE, FALSE, TRUE)
    expect_equal(recovery_vec[1], TRUE)
    message("  Error recovery test passed")

    cleanup_fmalloc()
})()

message("Memory management stress tests completed!")
