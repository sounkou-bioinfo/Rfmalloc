library(tinytest)
library(Rgguf)

message("Testing owned Rfmalloc tensors and borrowed GGUF views...")

(function() {
    message("  Test 1: gguf_tensor() result is an fmalloc ALTREP vector")
    tmp <- tempfile(fileext = ".gguf")
    alloc_file <- tempfile(fileext = ".bin")
    on.exit({
        unlink(tmp)
        unlink(alloc_file)
    }, add = TRUE)

    m <- matrix(as.double(1:20), nrow = 4, ncol = 5)
    gguf_write_tensors(tmp, list(m = m))

    rt <- Rfmalloc::open_fmalloc(alloc_file)
    got <- gguf_tensor(tmp, "m", runtime = rt)

    expect_true(Rfmalloc::is_fmalloc_vector(got))
    expect_true(inherits(got, "fmalloc"))
    expect_equal(as.vector(got), as.vector(m))

    message("  fmalloc-backed tensor test passed")
})()

(function() {
    message("  Test 2: matrix product of two imported tensors matches base R")
    tmp <- tempfile(fileext = ".gguf")
    alloc_file <- tempfile(fileext = ".bin")
    on.exit({
        unlink(tmp)
        unlink(alloc_file)
    }, add = TRUE)

    set.seed(1)
    a <- matrix(as.double(1:12), nrow = 3, ncol = 4)
    b <- matrix(as.double(1:8), nrow = 4, ncol = 2)
    gguf_write_tensors(tmp, list(a = a, b = b))

    rt <- Rfmalloc::open_fmalloc(alloc_file)
    mats <- gguf_import(tmp, runtime = rt)

    expect_true(Rfmalloc::is_fmalloc_vector(mats$a))
    expect_true(Rfmalloc::is_fmalloc_vector(mats$b))

    product <- mats$a %*% mats$b
    expect_equal(dim(product), dim(a %*% b))
    expect_equal(as.vector(product), as.vector(a %*% b))
    expect_true(Rfmalloc::is_fmalloc_vector(product))

    cp <- crossprod(mats$a)
    expect_equal(dim(cp), dim(crossprod(a)))
    expect_equal(as.vector(cp), as.vector(crossprod(a)))

    message("  Matrix product test passed")
})()

(function() {
    message("  Test 3: a borrowed GGUF view performs no payload copy")
    tmp <- tempfile(fileext = ".gguf")
    alloc_file <- tempfile(fileext = ".bin")
    on.exit({
        unlink(tmp)
        unlink(alloc_file)
    }, add = TRUE)

    m <- matrix(as.double(1:20), nrow = 4L, ncol = 5L)
    gguf_write_tensors(tmp, list(m = m))
    rt <- Rfmalloc::open_fmalloc(alloc_file)

    view <- gguf_tensor(tmp, "m", runtime = rt, as = "view")
    expect_identical(typeof(view), "externalptr")
    expect_false(Rfmalloc::is_fmalloc_vector(view))
    expect_true(inherits(view, "fmalloc_tensor"))
    expect_equal(dim(view), dim(m))
    expect_identical(
        Rfmalloc::fmalloc_storage_advise(view, "sequential"),
        view
    )

    # The view retains the short-lived context opened from `tmp`.
    invisible(gc())
    back <- Rfmalloc::fmalloc_tensor_materialize(view)
    expect_identical(back[], m)
    lhs <- matrix(as.double(1:8), nrow = 2L, ncol = 4L)
    expect_equal((lhs %*% view)[], lhs %*% m)
    Rfmalloc::fmalloc_storage_advise(view, "dontneed")

    message("  Borrowed-view test passed")
})()
