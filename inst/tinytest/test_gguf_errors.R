library(tinytest)
library(Rgguf)

message("Testing GGUF error paths...")

(function() {
    message("  Test 1: gguf_open() on a nonexistent file errors cleanly")
    tmp <- tempfile(fileext = ".gguf")
    expect_false(file.exists(tmp))
    expect_error(gguf_open(tmp), pattern = "no such file")

    message("  Nonexistent file test passed")
})()

(function() {
    message("  Test 2: gguf_open() on an invalid (non-GGUF) file errors cleanly")
    tmp <- tempfile(fileext = ".gguf")
    writeLines("not a gguf file", tmp)
    on.exit(unlink(tmp), add = TRUE)

    expect_error(gguf_open(tmp), pattern = "failed to open")

    message("  Invalid file test passed")
})()

(function() {
    message("  Test 3: gguf_tensor() on an unknown tensor name errors cleanly")
    tmp <- tempfile(fileext = ".gguf")
    rt <- Rfmalloc::open_fmalloc(tempfile(fileext = ".bin"), mode = "scratch")
    on.exit({
        unlink(tmp)
        Rfmalloc::cleanup_fmalloc(rt)
    }, add = TRUE)

    gguf_write_tensors(tmp, list(w = matrix(as.double(1:4), nrow = 2)))

    expect_error(gguf_tensor(tmp, "does_not_exist", runtime = rt), pattern = "no such tensor")

    message("  Unknown tensor name test passed")
})()

(function() {
    message("  Test 4: gguf_import() with an unknown tensor name errors cleanly")
    tmp <- tempfile(fileext = ".gguf")
    rt <- Rfmalloc::open_fmalloc(tempfile(fileext = ".bin"), mode = "scratch")
    on.exit({
        unlink(tmp)
        Rfmalloc::cleanup_fmalloc(rt)
    }, add = TRUE)

    gguf_write_tensors(tmp, list(w = matrix(as.double(1:4), nrow = 2)))

    expect_error(gguf_import(tmp, tensors = "nope", runtime = rt), pattern = "no such tensor")

    message("  gguf_import unknown tensor test passed")
})()

(function() {
    message("  Test 5: gguf_write_tensors() rejects non-numeric / badly named input")
    tmp <- tempfile(fileext = ".gguf")
    on.exit(unlink(tmp), add = TRUE)

    expect_error(gguf_write_tensors(tmp, list(w = "not numeric")))
    expect_error(gguf_write_tensors(tmp, list(1:3)))
    expect_error(gguf_write_tensors(tmp, list()))

    message("  gguf_write_tensors() validation test passed")
})()

(function() {
    message("  Test 6: gguf_metadata()/gguf_tensors()/gguf_tensor() reject a bad x argument")
    expect_error(gguf_metadata(123), pattern = "gguf_ctx")
    expect_error(gguf_tensors(list()), pattern = "gguf_ctx")
    expect_error(gguf_tensor(TRUE, "w"), pattern = "gguf_ctx")

    message("  Bad argument test passed")
})()
