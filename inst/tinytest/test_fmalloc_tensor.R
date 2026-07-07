library(tinytest)
library(Rfmalloc)

message("Testing typed fmalloc tensors and the panel-streaming matmul engine...")

.f32_payload <- function(rt, values) {
    bytes <- writeBin(as.numeric(values), raw(), size = 4L, endian = "little")
    payload <- create_fmalloc_vector("raw", length(bytes), runtime = rt,
        zero_initialize = FALSE)
    payload[] <- bytes
    payload
}

(function() {
    message("  Test 1: builtin codecs are registered and introspectable")
    codecs <- fmalloc_tensor_codecs()
    expect_true(all(c("f64", "f32", "f16", "bf16") %in% codecs))
    expect_error(create_fmalloc_tensor(raw(1), "no_such_codec", c(1L, 1L)),
        "unknown fmalloc tensor codec")
})()

(function() {
    message("  Test 2: f32 tensor matmul matches base R (both operand sides)")
    tmp <- tempfile(fileext = ".bin")
    rt <- open_fmalloc(tmp, mode = "persistent")
    on.exit({
        cleanup_fmalloc(rt)
        unlink(tmp)
    }, add = TRUE)

    set.seed(11)
    a_vals <- round(rnorm(100 * 50), 3)
    ten <- create_fmalloc_tensor(.f32_payload(rt, a_vals), "f32", c(100L, 50L))
    expect_equal(dim(ten), c(100L, 50L))
    expect_equal(fmalloc_tensor_dtype(ten), "f32")

    # Reference values: widen the payload through materialize, then compare
    # the streamed engine against base R on the widened doubles.
    a_mat <- fmalloc_tensor_materialize(ten)
    expect_true(inherits(a_mat, "fmalloc_matrix"))
    expect_true(is_fmalloc_vector(a_mat))
    a_base <- matrix(a_mat[], nrow = 100)

    b <- matrix(rnorm(50 * 30), nrow = 50)
    z <- ten %*% b
    expect_true(inherits(z, "fmalloc_matrix"))
    expect_true(is_fmalloc_vector(z))
    expect_equal(dim(z), c(100L, 30L))
    expect_equal(as.vector(z), as.vector(a_base %*% b))

    d <- matrix(rnorm(40 * 100), nrow = 40)
    z2 <- d %*% ten
    expect_true(inherits(z2, "fmalloc_matrix"))
    expect_equal(dim(z2), c(40L, 50L))
    expect_equal(as.vector(z2), as.vector(d %*% a_base))

    # Vector promotion follows base rules.
    v <- rnorm(50)
    zv <- ten %*% v
    expect_equal(dim(zv), c(100L, 1L))
    expect_equal(as.vector(zv), as.vector(a_base %*% v))
})()

(function() {
    message("  Test 3: multi-panel streaming agrees with single-panel")
    tmp <- tempfile(fileext = ".bin")
    rt <- open_fmalloc(tmp, mode = "persistent")
    on.exit({
        cleanup_fmalloc(rt)
        unlink(tmp)
        options(Rfmalloc.tensor_panel_elems = NULL)
    }, add = TRUE)

    set.seed(12)
    ten <- create_fmalloc_tensor(.f32_payload(rt, rnorm(64 * 33)), "f32", c(64L, 33L))
    b <- matrix(rnorm(33 * 7), nrow = 33)
    d <- matrix(rnorm(5 * 64), nrow = 5)

    options(Rfmalloc.tensor_panel_elems = NULL)
    z_one <- ten %*% b
    z2_one <- d %*% ten
    # Force tiny panels (64 elements = one column per panel).
    options(Rfmalloc.tensor_panel_elems = 64)
    z_many <- ten %*% b
    z2_many <- d %*% ten

    expect_equal(as.vector(z_many), as.vector(z_one))
    expect_equal(as.vector(z2_many), as.vector(z2_one))
})()

(function() {
    message("  Test 4: f16/bf16 decode known bit patterns")
    tmp <- tempfile(fileext = ".bin")
    rt <- open_fmalloc(tmp, mode = "persistent")
    on.exit({
        cleanup_fmalloc(rt)
        unlink(tmp)
    }, add = TRUE)

    # f16: 1.0, -2.0, 0.5, smallest subnormal, Inf, 0
    h <- c(0x3C00L, 0xC000L, 0x3800L, 0x0001L, 0x7C00L, 0x0000L)
    bytes <- writeBin(as.integer(h), raw(), size = 2L, endian = "little")
    payload <- create_fmalloc_vector("raw", length(bytes), runtime = rt,
        zero_initialize = FALSE)
    payload[] <- bytes
    ten <- create_fmalloc_tensor(payload, "f16", c(2L, 3L))
    vals <- fmalloc_tensor_materialize(ten)
    expect_equal(as.vector(vals[]), c(1, -2, 0.5, 2^-24, Inf, 0))

    # bf16 payload = top 2 bytes of each little-endian f32
    x <- c(1.5, -3.25, 100, 0)
    f32 <- writeBin(x, raw(), size = 4L, endian = "little")
    keep <- rep(c(FALSE, FALSE, TRUE, TRUE), length(x))
    payload2 <- create_fmalloc_vector("raw", sum(keep), runtime = rt,
        zero_initialize = FALSE)
    payload2[] <- f32[keep]
    ten2 <- create_fmalloc_tensor(payload2, "bf16", c(2L, 2L))
    vals2 <- fmalloc_tensor_materialize(ten2)
    expect_equal(as.vector(vals2[]), x)
})()

(function() {
    message("  Test 5: non-finite dense operands take the materialized NA path")
    tmp <- tempfile(fileext = ".bin")
    rt <- open_fmalloc(tmp, mode = "persistent")
    on.exit({
        cleanup_fmalloc(rt)
        unlink(tmp)
    }, add = TRUE)

    set.seed(13)
    ten <- create_fmalloc_tensor(.f32_payload(rt, rnorm(20 * 10)), "f32", c(20L, 10L))
    a_base <- matrix(fmalloc_tensor_materialize(ten)[], nrow = 20)

    b <- matrix(rnorm(10 * 4), nrow = 10)
    b[3, 2] <- NA_real_
    z <- ten %*% b
    expect_equal(as.vector(z), as.vector(a_base %*% b))

    # tensor x tensor and crossprod paths materialize.
    z2 <- crossprod(ten)
    expect_equal(as.vector(z2), as.vector(crossprod(a_base)))
    z3 <- tcrossprod(ten)
    expect_equal(as.vector(z3), as.vector(tcrossprod(a_base)))
})()

(function() {
    message("  Test 6: payload validation")
    tmp <- tempfile(fileext = ".bin")
    rt <- open_fmalloc(tmp, mode = "persistent")
    on.exit({
        cleanup_fmalloc(rt)
        unlink(tmp)
    }, add = TRUE)

    short <- create_fmalloc_vector("raw", 10, runtime = rt)
    expect_error(create_fmalloc_tensor(short, "f32", c(2L, 3L)), "payload has")
    expect_error(create_fmalloc_tensor(raw(24), "f32", c(2L, 3L)),
        "must be an fmalloc raw vector")
})()

(function() {
    message("  Test 7: forced out-of-core path matches the in-core result")
    tmp <- tempfile(fileext = ".bin")
    rt <- open_fmalloc(tmp, mode = "persistent")
    on.exit({
        cleanup_fmalloc(rt)
        unlink(tmp)
        options(Rfmalloc.ooc_threshold_gb = NULL, Rfmalloc.tensor_panel_elems = NULL)
    }, add = TRUE)

    set.seed(21)
    vals <- round(rnorm(256 * 40), 3)
    tf <- create_fmalloc_tensor(.f32_payload(rt, vals), "f32", c(256L, 40L))
    mf <- matrix(fmalloc_tensor_materialize(tf)[], 256, 40)
    b <- matrix(rnorm(40 * 3), 40, 3)
    d <- matrix(rnorm(5 * 256), 5, 256)

    incore_r <- tf %*% b
    incore_l <- d %*% tf

    # Force the OOC path (fixed-geometry f32 => per-panel eviction) with tiny panels.
    options(Rfmalloc.ooc_threshold_gb = 0, Rfmalloc.tensor_panel_elems = 4096)
    ooc_r <- tf %*% b
    ooc_l <- d %*% tf
    expect_equal(as.vector(ooc_r[]), as.vector(incore_r[]))
    expect_equal(as.vector(ooc_l[]), as.vector(incore_l[]))
    expect_equal(as.vector(ooc_r[]), as.vector(mf %*% b))

    # Self-indexing ALP tensor: OOC forced, no eviction, still exact.
    xa <- round(runif(2048 * 8, 0, 2), 3)
    ma <- matrix(xa, 2048, 8)
    ta <- as_fmalloc_tensor(ma, runtime = rt)
    da <- matrix(rnorm(4 * 2048), 4, 2048)
    expect_equal(as.vector((da %*% ta)[]), as.vector(da %*% ma))
})()

(function() {
    message("  Test 8: n-dimensional tensors store/materialize; products stay 2-D")
    tmp <- tempfile(fileext = ".bin")
    rt <- open_fmalloc(tmp, mode = "persistent")
    on.exit({ cleanup_fmalloc(rt); unlink(tmp) }, add = TRUE)

    set.seed(31)
    A <- array(round(runif(20 * 15 * 4, 0, 2), 3), dim = c(20, 15, 4))
    ten <- as_fmalloc_tensor(A, dtype = "alp", runtime = rt)
    expect_equal(dim(ten), c(20L, 15L, 4L))

    back <- fmalloc_tensor_materialize(ten)
    expect_true(is.array(back))
    expect_equal(dim(back), c(20L, 15L, 4L))
    expect_identical(as.vector(back[]), as.vector(A))

    # sparse codec works n-D too
    S <- array(0, dim = c(30, 10, 3))
    S[sample(length(S), length(S) * 0.1)] <- rpois(round(length(S) * 0.1), 2) + 1
    tenS <- as_fmalloc_tensor(S, dtype = "sparse", runtime = rt)
    expect_identical(as.vector(fmalloc_tensor_materialize(tenS)[]), as.vector(S))

    # a matrix product on a >2-D tensor errors clearly
    b <- matrix(rnorm(4), 4, 1)
    expect_error(ten %*% b, "2-dimensional")

    # a bare vector encodes as n x 1 and still multiplies
    v <- as_fmalloc_tensor(round(runif(50, 0, 2), 3), dtype = "alp", runtime = rt)
    expect_equal(dim(v), c(50L, 1L))
})()

message("fmalloc tensor tests completed")
