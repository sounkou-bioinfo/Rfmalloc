library(tinytest)
library(Rgguf)

message("Testing GGUF write/read round-trips (validates dim-order mapping)...")

# gguf_tensor()/gguf_import() allocate their destination via
# Rfmalloc::create_fmalloc_matrix()/create_fmalloc_array(), which requires
# either an explicit runtime or a previously-initialized default one. Each
# test below opens its own scratch runtime so tests stay isolated from each
# other and from global Rfmalloc state.
new_runtime <- function() {
    Rfmalloc::open_fmalloc(tempfile(fileext = ".bin"), mode = "scratch")
}

(function() {
    message("  Test 1: matrix round-trip matches base R exactly")
    tmp <- tempfile(fileext = ".gguf")
    rt <- new_runtime()
    on.exit({
        unlink(tmp)
        Rfmalloc::cleanup_fmalloc(rt)
    }, add = TRUE)

    m <- matrix(as.double(1:12), nrow = 3, ncol = 4)
    gguf_write_tensors(tmp, list(m = m))

    got <- gguf_tensor(tmp, "m", runtime = rt)
    expect_equal(dim(got), dim(m))
    expect_equal(as.vector(got), as.vector(m))

    message("  Matrix round-trip test passed")
})()

(function() {
    message("  Test 2: non-square matrix preserves nrow/ncol (rules out transposition bugs)")
    tmp <- tempfile(fileext = ".gguf")
    rt <- new_runtime()
    on.exit({
        unlink(tmp)
        Rfmalloc::cleanup_fmalloc(rt)
    }, add = TRUE)

    m <- matrix(as.double(seq_len(30)), nrow = 5, ncol = 6)
    gguf_write_tensors(tmp, list(m = m))

    got <- gguf_tensor(tmp, "m", runtime = rt)
    expect_equal(nrow(got), 5L)
    expect_equal(ncol(got), 6L)
    expect_equal(got[2, 3], m[2, 3])
    expect_equal(got[5, 1], m[5, 1])
    expect_equal(as.vector(got), as.vector(m))

    message("  Non-square matrix round-trip test passed")
})()

(function() {
    message("  Test 3: 3D array round-trip")
    tmp <- tempfile(fileext = ".gguf")
    rt <- new_runtime()
    on.exit({
        unlink(tmp)
        Rfmalloc::cleanup_fmalloc(rt)
    }, add = TRUE)

    a <- array(as.double(seq_len(24)), dim = c(2, 3, 4))
    gguf_write_tensors(tmp, list(a = a))

    got <- gguf_tensor(tmp, "a", runtime = rt)
    expect_equal(dim(got), dim(a))
    expect_equal(as.vector(got), as.vector(a))
    expect_equal(got[1, 2, 3], a[1, 2, 3])
    expect_equal(got[2, 1, 4], a[2, 1, 4])

    message("  3D array round-trip test passed")
})()

(function() {
    message("  Test 4: plain vector round-trips as a 1-dimensional array")
    tmp <- tempfile(fileext = ".gguf")
    rt <- new_runtime()
    on.exit({
        unlink(tmp)
        Rfmalloc::cleanup_fmalloc(rt)
    }, add = TRUE)

    v <- as.double(11:20)
    gguf_write_tensors(tmp, list(v = v))

    got <- gguf_tensor(tmp, "v", runtime = rt)
    expect_equal(as.vector(got), v)
    expect_equal(length(got), length(v))

    message("  Vector round-trip test passed")
})()

(function() {
    message("  Test 5: multiple tensors in one file round-trip independently")
    tmp <- tempfile(fileext = ".gguf")
    rt <- new_runtime()
    on.exit({
        unlink(tmp)
        Rfmalloc::cleanup_fmalloc(rt)
    }, add = TRUE)

    m1 <- matrix(as.double(1:6), nrow = 2, ncol = 3)
    m2 <- matrix(as.double(101:112), nrow = 4, ncol = 3)
    gguf_write_tensors(tmp, list(first = m1, second = m2))

    got1 <- gguf_tensor(tmp, "first", runtime = rt)
    got2 <- gguf_tensor(tmp, "second", runtime = rt)
    expect_equal(as.vector(got1), as.vector(m1))
    expect_equal(as.vector(got2), as.vector(m2))
    expect_equal(dim(got1), dim(m1))
    expect_equal(dim(got2), dim(m2))

    message("  Multi-tensor round-trip test passed")
})()
