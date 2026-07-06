library(tinytest)
library(Rfmalloc)

message("Testing public Rfmalloc API helpers...")

(function() {
    message("  Test 1: runtime and vector introspection helpers")
    tmp <- tempfile(fileext = ".bin")
    rt <- open_fmalloc(tmp, mode = "persistent")
    on.exit({
        cleanup_fmalloc(rt)
        unlink(tmp)
    }, add = TRUE)

    expect_true(fmalloc_api_version() >= 3L)
    expect_true(is_fmalloc_runtime(rt))
    expect_false(is_fmalloc_runtime(NULL))

    x <- create_fmalloc_vector("integer", 5, runtime = rt)
    x[] <- seq_len(5)

    expect_true(is_fmalloc_vector(x))
    expect_false(is_fmalloc_vector(seq_len(5)))
    expect_equal(fmalloc_vector_type(x), "integer")
    expect_equal(fmalloc_vector_type(x, label = FALSE), 13L)
    expect_equal(fmalloc_vector_length(x), 5)

    payload <- fmalloc_vector_payload_ptr(x)
    expect_equal(typeof(payload), "externalptr")

    info <- fmalloc_vector_info(x)
    expect_equal(info$type, "integer")
    expect_equal(info$sexptype, 13L)
    expect_equal(info$length, 5)
    expect_true(info$payload_nbytes >= 20)
    expect_true(info$runtime_open)
    expect_true(info$recoverable)

    runtime_info <- fmalloc_runtime_info(rt)
    expect_equal(runtime_info$mode, "persistent")
    expect_true(runtime_info$runtime_open)

    rt_from_x <- fmalloc_runtime(x)
    expect_true(is_fmalloc_runtime(rt_from_x))
    rm(rt_from_x)
    gc()

    message("  Runtime and vector introspection tests passed")
})()

(function() {
    message("  Test 2: default runtime is synchronized across R and native API")
    tmp <- tempfile(fileext = ".bin")
    on.exit({
        cleanup_fmalloc()
        unlink(tmp)
    }, add = TRUE)

    initialized <- init_fmalloc(tmp, mode = "scratch")
    expect_true(is.logical(initialized))

    default <- fmalloc_default_runtime()
    expect_true(is_fmalloc_runtime(default))

    x <- create_fmalloc_vector("integer", 3)
    x[] <- c(10L, 20L, 30L)
    expect_true(is_fmalloc_vector(x))
    expect_equal(fmalloc_runtime_info()$mode, "scratch")

    cleanup_fmalloc()
    expect_true(is.null(fmalloc_default_runtime()))

    message("  Default runtime synchronization test passed")
})()

message("Public API helper tests completed")
