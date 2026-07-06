library(tinytest)
library(Rgguf)

message("Testing gguf_metadata() and gguf_tensors()...")

(function() {
    message("  Test 1: gguf_metadata() returns written scalar values by name")
    tmp <- tempfile(fileext = ".gguf")
    on.exit(unlink(tmp), add = TRUE)

    gguf_write_tensors(tmp,
        tensors = list(w = matrix(as.double(1:6), nrow = 2)),
        metadata = list(
            name = "example-model",
            version = 3,
            temperature = 0.75
        )
    )

    md <- gguf_metadata(tmp)
    expect_true(is.list(md))
    expect_equal(md$name, "example-model")
    expect_equal(md$version, 3)
    expect_equal(md$temperature, 0.75)

    message("  Metadata scalar test passed")
})()

(function() {
    message("  Test 2: gguf_metadata() on a file with no metadata returns an empty named list")
    tmp <- tempfile(fileext = ".gguf")
    on.exit(unlink(tmp), add = TRUE)

    gguf_write_tensors(tmp, list(w = 1:5 + 0.0))

    md <- gguf_metadata(tmp)
    expect_true(is.list(md))
    expect_equal(length(md), 0L)

    message("  Empty metadata test passed")
})()

(function() {
    message("  Test 3: gguf_tensors() lists name/type/dims/n_elements correctly")
    tmp <- tempfile(fileext = ".gguf")
    on.exit(unlink(tmp), add = TRUE)

    m <- matrix(as.double(1:20), nrow = 4, ncol = 5)
    v <- as.double(1:7)
    gguf_write_tensors(tmp, list(weight = m, bias = v))

    tbl <- gguf_tensors(tmp)
    expect_true(is.data.frame(tbl))
    expect_equal(nrow(tbl), 2L)
    expect_true(all(c("name", "type", "n_dims", "dims", "n_elements", "nbytes", "offset") %in% names(tbl)))

    expect_equal(sort(tbl$name), sort(c("weight", "bias")))
    expect_true(all(tbl$type == "f32"))

    weight_row <- tbl[tbl$name == "weight", ]
    expect_equal(weight_row$n_dims, 2L)
    expect_equal(weight_row$dims[[1]], c(4L, 5L))
    expect_equal(weight_row$n_elements, 20)

    bias_row <- tbl[tbl$name == "bias", ]
    expect_equal(bias_row$n_dims, 1L)
    expect_equal(bias_row$dims[[1]], 7L)
    expect_equal(bias_row$n_elements, 7)

    message("  Tensor listing test passed")
})()

(function() {
    message("  Test 4: gguf_open() context can be reused across multiple calls")
    tmp <- tempfile(fileext = ".gguf")
    rt <- Rfmalloc::open_fmalloc(tempfile(fileext = ".bin"), mode = "scratch")
    on.exit({
        unlink(tmp)
        Rfmalloc::cleanup_fmalloc(rt)
    }, add = TRUE)

    gguf_write_tensors(tmp, list(a = 1:3 + 0.0, b = 4:6 + 0.0))

    ctx <- gguf_open(tmp)
    expect_true(inherits(ctx, "gguf_ctx"))

    tbl1 <- gguf_tensors(ctx)
    tbl2 <- gguf_tensors(ctx)
    expect_equal(tbl1$name, tbl2$name)

    got_a <- gguf_tensor(ctx, "a", runtime = rt)
    expect_equal(as.vector(got_a), c(1, 2, 3))

    message("  Reused context test passed")
})()
