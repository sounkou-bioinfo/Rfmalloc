library(tinytest)
library(Rfmalloc)

message("Testing explicit destroy for fmalloc vectors...")

(function() {
    message("  Test 1: Scratch explicit destroy and invalidation")
    tmp <- tempfile(fileext = ".bin")
    on.exit({
        cleanup_fmalloc(rt)
        unlink(tmp)
    }, add = TRUE)

    rt <- open_fmalloc(tmp, mode = "scratch", size_gb = 0.05)
    x <- create_fmalloc_vector("integer", 4, runtime = rt)
    x[] <- 1:4

    expect_true(destroy_fmalloc_vector(x))
    expect_false(destroy_fmalloc_vector(x))
    expect_error(x[1], "closed fmalloc ALTREP vector handle")

    message("  Scratch explicit destroy passed")
})()

(function() {
    message("  Test 2: Persistent explicit destroy defaults to non-recoverable metadata")
    tmp <- tempfile(fileext = ".bin")
    on.exit({
        cleanup_fmalloc(rt)
        unlink(tmp)
    }, add = TRUE)

    rt <- open_fmalloc(tmp, mode = "persistent")
    x <- create_fmalloc_vector("integer", 3, runtime = rt)
    x[] <- c(10L, 20L, 30L)

    blob <- serialize(x, NULL)
    state_before <- list_fmalloc_allocations(rt)
    expect_true(any(state_before$state == "committed"))

    expect_true(destroy_fmalloc_vector(x))
    expect_error(unserialize(blob), "serialized fmalloc catalog record is not committed|tombstone")

    state_after <- list_fmalloc_allocations(rt)
    expect_true(any(state_after$state == "tombstone"))

    message("  Persistent destroy metadata test passed")
})()

(function() {
    message("  Test 3: Destroy fails when vector is still referenced by list child")
    tmp <- tempfile(fileext = ".bin")
    on.exit({
        cleanup_fmalloc(rt)
        unlink(tmp)
    }, add = TRUE)

    rt <- open_fmalloc(tmp, mode = "persistent")

    parent <- create_fmalloc_vector("list", 1, runtime = rt)
    second_parent <- create_fmalloc_vector("list", 1, runtime = rt)
    child <- create_fmalloc_vector("integer", 2, runtime = rt)
    child[] <- 1:2
    parent[[1]] <- child
    second_parent[[1]] <- child

    expect_error(
        destroy_fmalloc_vector(child),
        "parent reference"
    )

    parent[[1]] <- NULL
    expect_error(
        destroy_fmalloc_vector(child),
        "parent reference"
    )

    second_parent[[1]] <- NULL
    expect_true(destroy_fmalloc_vector(child))

    message("  Destroy failure on live references test passed")
})()

(function() {
    message("  Test 4: Persistent unsafe destroy keeps metadata non-recoverable")
    tmp <- tempfile(fileext = ".bin")
    on.exit({
        cleanup_fmalloc(rt)
        unlink(tmp)
    }, add = TRUE)

    rt <- open_fmalloc(tmp, mode = "persistent")
    x <- create_fmalloc_vector("character", 2, runtime = rt)
    x[] <- c("alpha", "beta")

    blob <- serialize(x, NULL)
    expect_true(destroy_fmalloc_vector(x, unsafe = TRUE))

    state <- list_fmalloc_allocations(rt)
    expect_true(any(state$state == "tombstone"))
    expect_error(unserialize(blob), "serialized fmalloc catalog record is not committed|tombstone")

    message("  Persistent unsafe destroy test passed")
})()

message("Explicit destroy tests for fmalloc vectors completed!")
