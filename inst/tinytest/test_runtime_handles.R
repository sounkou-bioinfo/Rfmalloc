library(tinytest)
library(Rfmalloc)

message("Testing fmalloc runtime handles and ALTREP lifetime links...")

(function() {
    file1 <- tempfile(fileext = ".bin")
    file2 <- tempfile(fileext = ".bin")
    rt1 <- open_fmalloc(file1)
    rt2 <- open_fmalloc(file2, size_gb = 0.1)
    on.exit({
        cleanup_fmalloc(rt1)
        cleanup_fmalloc(rt2)
        unlink(c(file1, file2))
    }, add = TRUE)

    expect_true(inherits(rt1, "fmalloc_runtime"))
    expect_true(inherits(rt2, "fmalloc_runtime"))
    expect_true(isTRUE(attr(rt1, "initialized")))
    expect_true(isTRUE(attr(rt2, "initialized")))
    expect_equal(attr(rt1, "mode"), "persistent")
    expect_equal(attr(rt2, "mode"), "persistent")
    expect_true(is.character(attr(rt1, "uuid")))

    v1 <- create_fmalloc_vector("integer", 10, runtime = rt1)
    v2 <- create_fmalloc_vector("numeric", 10, runtime = rt2)
    v1[1] <- 11L
    v2[1] <- 22

    cleanup_fmalloc(rt1)
    expect_equal(v1[1], 11L)
    expect_error(
        create_fmalloc_vector("integer", 10, runtime = rt1),
        "runtime is closed"
    )

    v2b <- create_fmalloc_vector("integer", 10, runtime = rt2)
    v2b[1] <- 33L
    expect_equal(v2[1], 22)
    expect_equal(v2b[1], 33L)

    rm(v1)
    gc()

    cleanup_fmalloc(rt2)
    rm(v2, v2b)
    gc()

    message("  Explicit runtime handle test passed")
})()

(function() {
    scratch_file <- tempfile(fileext = ".bin")
    scratch_rt <- open_fmalloc(scratch_file, mode = "scratch")
    on.exit({
        cleanup_fmalloc(scratch_rt)
        unlink(scratch_file)
    }, add = TRUE)

    expect_equal(attr(scratch_rt, "mode"), "scratch")
    cleanup_fmalloc(scratch_rt)
    message("  Scratch runtime mode test passed")
})()

(function() {
    file3 <- tempfile(fileext = ".bin")
    on.exit(unlink(file3), add = TRUE)

    kept_vector <- local({
        rt <- open_fmalloc(file3)
        x <- create_fmalloc_vector("integer", 10, runtime = rt)
        x[1] <- 44L
        x
    })

    gc()
    expect_equal(kept_vector[1], 44L)

    rm(kept_vector)
    gc()

    message("  Runtime-drop while vector-live test passed")
})()

message("Runtime handle tests completed!")
