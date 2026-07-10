library(tinytest)
library(Rfmalloc)

message("Testing the banded LD store (quantized correlations, a sibling of the tensor codec ABI)...")

## Build a small known banded symmetric correlation matrix, extract its band as
## COO triplets, round-trip through the ld codec, and check ld_pair()/ld_col()
## reconstruct it to the quantization tolerance, with correct band edges, unit
## diagonal, and 0 for out-of-window pairs.

make_banded <- function(m, w, seed) {
    set.seed(seed)
    M <- matrix(0, m, m)
    diag(M) <- 1
    for (a in seq_len(m - 1L)) {
        for (b in (a + 1L):min(m, a + w)) {
            v <- runif(1, -0.9, 0.9)
            M[a, b] <- v
            M[b, a] <- v
        }
    }
    M
}

# All (i, j) triplets within band half-width w (contiguous band incl. diagonal).
band_triplets <- function(M, w) {
    m <- nrow(M)
    i <- integer(0); j <- integer(0); x <- numeric(0)
    for (col in seq_len(m)) {
        rows <- max(1L, col - w):min(m, col + w)
        i <- c(i, rows)
        j <- c(j, rep(col, length(rows)))
        x <- c(x, M[rows, col])
    }
    list(i = i, j = j, x = x)
}

(function() {
    message("  Test 1: int8 round-trip to ~1/127, band edges, diagonal, out-of-window = 0")
    tmp <- tempfile(fileext = ".bin")
    rt <- open_fmalloc(tmp, mode = "scratch", size_gb = 0.1)
    on.exit({ cleanup_fmalloc(rt); unlink(tmp) }, add = TRUE)

    m <- 12L; w <- 3L
    M <- make_banded(m, w, seed = 1L)
    tr <- band_triplets(M, w)

    corr <- fmalloc_ld(tr$i, tr$j, tr$x, n_variants = m, bits = 8L,
                       window = w, runtime = rt)
    expect_true(inherits(corr, "fmalloc_ld"))
    expect_false(inherits(corr, "fmalloc_tensor"))   # sibling, not a codec
    expect_equal(ld_ncol(corr), m)
    expect_equal(dim(corr), c(m, m))

    tol8 <- 1 / 127 + 1e-9

    ## every in-band pair matches to the int8 tolerance, both orientations
    for (col in seq_len(m)) {
        for (row in seq_len(m)) {
            got <- ld_pair(corr, row, col)
            if (abs(row - col) <= w) {
                expect_true(abs(got - M[row, col]) <= tol8,
                            info = sprintf("in-band (%d,%d): got %.4f exp %.4f",
                                           row, col, got, M[row, col]))
            } else {
                expect_equal(got, 0)   # out-of-window pair is absent -> 0
            }
        }
    }

    ## symmetry of the stored band
    for (col in seq_len(m)) for (row in seq_len(m))
        expect_equal(ld_pair(corr, row, col), ld_pair(corr, col, row))

    ## diagonal is exactly 1 (127/127)
    for (jj in seq_len(m)) expect_equal(ld_pair(corr, jj, jj), 1)

    ## ld_col: band edges and values for a middle column and the two ends
    for (col in c(1L, 5L, m)) {
        cc <- ld_col(corr, col)
        expect_equal(cc$lo, max(1L, col - w))
        expect_equal(cc$hi, min(m, col + w))
        expect_equal(length(cc$x), cc$hi - cc$lo + 1L)
        expect_true(max(abs(cc$x - M[cc$lo:cc$hi, col])) <= tol8)
    }

    ## input validation
    expect_error(fmalloc_ld(1:3, 1:2, c(1, 2, 3), 5, runtime = rt), "same length")
    expect_error(fmalloc_ld(1, 1, 1, 5, bits = 7L, runtime = rt), "bits")
    expect_error(ld_pair(corr, 99, 1))   # index out of range
})()

(function() {
    message("  Test 2: int16 round-trip to ~3e-5")
    tmp <- tempfile(fileext = ".bin")
    rt <- open_fmalloc(tmp, mode = "scratch", size_gb = 0.1)
    on.exit({ cleanup_fmalloc(rt); unlink(tmp) }, add = TRUE)

    m <- 20L; w <- 4L
    M <- make_banded(m, w, seed = 7L)
    tr <- band_triplets(M, w)

    corr <- fmalloc_ld(tr$i, tr$j, tr$x, n_variants = m, bits = 16L, runtime = rt)
    tol16 <- 1 / 32767 + 1e-9

    for (col in seq_len(m)) {
        cc <- ld_col(corr, col)
        expect_true(max(abs(cc$x - M[cc$lo:cc$hi, col])) <= tol16)
    }
    ## a far-apart pair is out of band
    expect_equal(ld_pair(corr, 1L, m), 0)
    ## diagonal exact
    expect_equal(ld_pair(corr, 10L, 10L), 1)
})()

(function() {
    message("  Test 3: compression - a banded store is a small fraction of the dense p x p")
    tmp <- tempfile(fileext = ".bin")
    rt <- open_fmalloc(tmp, mode = "scratch", size_gb = 0.2)
    on.exit({ cleanup_fmalloc(rt); unlink(tmp) }, add = TRUE)

    m <- 400L; w <- 10L
    M <- make_banded(m, w, seed = 3L)
    tr <- band_triplets(M, w)
    corr <- fmalloc_ld(tr$i, tr$j, tr$x, n_variants = m, bits = 8L, runtime = rt)

    bytes_band <- length(unclass(corr))
    bytes_dense <- as.double(m) * m * 8      # dense double p x p
    expect_true(bytes_dense / bytes_band > 20)   # banded int8 << dense f64
    ## a middle column has the full 2w+1 band
    expect_equal(length(ld_col(corr, 200L)$x), 2L * w + 1L)
})()
