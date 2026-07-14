library(tinytest)
library(Rfmalloc)

message("Testing the locus-major phased-haplotype store...")

(function() {
    message("  Test 1: exact round-trip, including a byte-boundary-crossing variant count")
    tmp <- tempfile(fileext = ".bin")
    rt <- open_fmalloc(tmp, mode = "scratch", size_gb = 0.2)
    on.exit({
        cleanup_fmalloc(rt)
        unlink(tmp)
    }, add = TRUE)

    set.seed(11L)
    # N %% 8 != 0, so every locus row has tail bits and SIMD padding.
    L <- 501L
    N <- 37L
    h <- matrix(sample(0:1, L * N, replace = TRUE), L, N)
    storage.mode(h) <- "integer"

    hap <- fmalloc_haplotypes(h, runtime = rt)
    expect_true(inherits(hap, "fmalloc_haplotypes"))
    # Sibling of the tensor codec ABI, not an instance of it.
    expect_false(inherits(hap, "fmalloc_tensor"))
    expect_equal(dim(hap), c(L, N))

    back <- fmalloc_hap_materialize(hap, runtime = rt)
    expect_true(is.matrix(back))
    expect_true(is.integer(back[]))
    expect_identical(dim(back), dim(h))
    expect_identical(back[], h)

    # One data bit per call plus one 64-byte-aligned record per locus.
    bits <- 8 * length(unclass(hap)) / (L * N)
    expect_true(bits > 1 && bits < 14)
})()

(function() {
    message("  Test 2: non-0/1 entries are rejected, not silently packed")
    tmp <- tempfile(fileext = ".bin")
    rt <- open_fmalloc(tmp, mode = "scratch", size_gb = 0.1)
    on.exit({
        cleanup_fmalloc(rt)
        unlink(tmp)
    }, add = TRUE)

    bad <- matrix(c(0L, 2L, 1L, 0L), 2L, 2L)
    expect_error(fmalloc_haplotypes(bad, runtime = rt), "0 or 1")

    bad_na <- matrix(c(0L, NA_integer_, 1L, 0L), 2L, 2L)
    expect_error(fmalloc_haplotypes(bad_na, runtime = rt), "0 or 1")

    expect_error(fmalloc_haplotypes(1:4, runtime = rt), "matrix")
})()

(function() {
    message("  Test 3: numeric/logical input is coerced; large panel compression ratio")
    tmp <- tempfile(fileext = ".bin")
    rt <- open_fmalloc(tmp, mode = "scratch", size_gb = 0.5)
    on.exit({
        cleanup_fmalloc(rt)
        unlink(tmp)
    }, add = TRUE)

    set.seed(12L)
    L <- 5000L
    N <- 400L
    h_logical <- matrix(sample(c(TRUE, FALSE), L * N, replace = TRUE), L, N)
    hap <- fmalloc_haplotypes(h_logical, runtime = rt)
    back <- fmalloc_hap_materialize(hap, runtime = rt)
    expect_identical(back[], matrix(as.integer(h_logical), L, N))

    bytes_bitpacked <- length(unclass(hap))
    bytes_integer <- L * N * 4L
    bytes_double <- L * N * 8L
    expect_true(bytes_integer / bytes_bitpacked > 24)
    expect_true(bytes_double / bytes_bitpacked > 48)
    # N = 400 needs 50 data bytes and one 64-byte SIMD row per locus.
    expect_true(8 * bytes_bitpacked / (L * N) < 1.3)
})()
