library(tinytest)
library(Rgguf)

message("Testing native (typed fmalloc_tensor) imports...")

(function() {
    message("  Test 1: quantized codecs are registered with Rfmalloc")
    codecs <- Rfmalloc::fmalloc_tensor_codecs()
    expect_true(all(c("q4_0", "q4_1", "q8_0", "q2_k", "q4_k", "q6_k") %in% codecs))
})()

(function() {
    message("  Test 2: native f32 import matches the numeric import")
    gguf_file <- tempfile(fileext = ".gguf")
    alloc_file <- tempfile(fileext = ".bin")
    rt <- Rfmalloc::open_fmalloc(alloc_file)
    on.exit({
        Rfmalloc::cleanup_fmalloc(rt)
        unlink(c(gguf_file, alloc_file))
    }, add = TRUE)

    set.seed(21)
    w <- matrix(rnorm(96 * 40), nrow = 96, ncol = 40)
    gguf_write_tensors(gguf_file, list(w = w))

    dense <- gguf_tensor(gguf_file, "w", runtime = rt)
    ten <- gguf_tensor(gguf_file, "w", runtime = rt, as = "native")

    expect_true(inherits(ten, "fmalloc_tensor"))
    expect_equal(Rfmalloc::fmalloc_tensor_dtype(ten), "f32")
    expect_equal(dim(ten), c(96L, 40L))

    mat <- Rfmalloc::fmalloc_tensor_materialize(ten)
    expect_equal(as.vector(mat[]), as.vector(dense[]))

    b <- matrix(rnorm(40 * 8), nrow = 40)
    z_native <- ten %*% b
    z_dense <- dense %*% b
    expect_true(Rfmalloc::is_fmalloc_vector(z_native))
    expect_equal(as.vector(z_native), as.vector(z_dense[]))

    x <- matrix(rnorm(5 * 96), nrow = 5)
    expect_equal(as.vector(x %*% ten), as.vector((x %*% dense)[]))
})()

(function() {
    message("  Test 3: gguf_import(as = \"native\") and error paths")
    gguf_file <- tempfile(fileext = ".gguf")
    alloc_file <- tempfile(fileext = ".bin")
    rt <- Rfmalloc::open_fmalloc(alloc_file)
    on.exit({
        Rfmalloc::cleanup_fmalloc(rt)
        unlink(c(gguf_file, alloc_file))
    }, add = TRUE)

    gguf_write_tensors(gguf_file, list(
        a = matrix(as.double(1:12), nrow = 4),
        v = as.double(1:5)
    ))

    tens <- gguf_import(gguf_file, tensors = "a", runtime = rt, as = "native")
    expect_true(inherits(tens$a, "fmalloc_tensor"))

    # 1-d tensors have no native mode.
    expect_error(gguf_tensor(gguf_file, "v", runtime = rt, as = "native"),
        "requires a 2-dimensional tensor")
})()

message("Native import tests completed")
