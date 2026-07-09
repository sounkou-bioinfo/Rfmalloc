library(tinytest)
library(Rfmalloc)

message("Testing the fractional dosage codec (1 byte/dosage, fixed-point)...")

# Fixed-point resolution: a dosage is stored as round(d * 127), so the round-trip
# error is bounded by half a step.
DOS_STEP <- 2 / 254
DOS_TOL <- DOS_STEP / 2 + 1e-9

(function() {
    message("  Test 1: round-trip within quantization, including NA and fractions")
    tmp <- tempfile(fileext = ".bin")
    rt <- open_fmalloc(tmp, mode = "scratch", size_gb = 0.5)
    on.exit({
        cleanup_fmalloc(rt)
        unlink(tmp)
    }, add = TRUE)

    expect_true("dosage" %in% fmalloc_tensor_codecs())

    m <- 400L
    n <- 30L
    set.seed(4L)
    d <- matrix(runif(m * n, 0, 2), m, n)
    d[1L, 1L] <- 0.3        # exercise specific fractional values
    d[2L, 1L] <- 1.7
    d[3L, 1L] <- 0          # boundaries
    d[4L, 1L] <- 2
    d[cbind(sample(m, 20L), sample(n, 20L, replace = TRUE))] <- NA

    tn <- fmalloc_dosage(d, runtime = rt)
    expect_equal(dim(tn), c(m, n))
    back <- matrix(fmalloc_tensor_materialize(tn)[], m, n)

    expect_true(all(is.na(back) == is.na(d)))
    expect_true(max(abs(back - d), na.rm = TRUE) <= DOS_TOL)
    # One byte per dosage, plus a 24-byte header. Never the 8 bytes of a double.
    expect_true(length(unclass(tn)) < m * n * 8 / 6)

    message("  Test 2: values outside [0, 2] are rejected, and only numeric input")
    expect_error(fmalloc_dosage(matrix(c(0, 2.5, 1, 2), 2L, 2L), runtime = rt),
                 "\\[0, 2\\]")
    expect_error(fmalloc_dosage(matrix(c(0, -0.1, 1, 2), 2L, 2L), runtime = rt),
                 "\\[0, 2\\]")
    expect_error(fmalloc_dosage(matrix("a", 1L, 1L), runtime = rt), "numeric")
})()

(function() {
    message("  Test 3: standardized decode matches scale() within quantization")
    tmp <- tempfile(fileext = ".bin")
    rt <- open_fmalloc(tmp, mode = "scratch", size_gb = 0.5)
    on.exit({
        cleanup_fmalloc(rt)
        unlink(tmp)
    }, add = TRUE)

    m <- 2000L
    n <- 60L
    set.seed(7L)
    d <- matrix(round(runif(m * n, 0, 2), 3), m, n)
    d[cbind(sample(m, 200L, replace = TRUE), sample(n, 200L, replace = TRUE))] <- NA

    # Reference: impute each column to its non-missing mean, then scale(). The
    # tensor works off quantized bytes, so the match is to quantization, not to
    # floating point.
    imp <- d
    mu <- colMeans(d, na.rm = TRUE)
    for (j in seq_len(n)) imp[is.na(imp[, j]), j] <- mu[j]
    ref <- scale(matrix(as.numeric(imp), m, n))

    ts <- fmalloc_dosage_standardize(fmalloc_dosage(d, runtime = rt), runtime = rt)
    got <- matrix(fmalloc_tensor_materialize(ts)[], m, n)

    # Standardized quantization error is (step/2)/sd_column; sd is O(0.5) here.
    expect_true(max(abs(got - ref)) < 0.05)
    expect_true(all(abs(got[is.na(d)]) < 1e-9))    # missing -> mean -> 0

    # Products agree with the dense standardized product to accumulated
    # quantization error: per-element ~step/2, amplified by sqrt of the
    # contraction length, so we check a relative bound rather than 1e-12.
    V <- matrix(rnorm(n * 4), n, 4)
    prod_t <- matrix((ts %*% V)[], m, 4)
    prod_r <- ref %*% V
    expect_true(max(abs(prod_t - prod_r)) / max(abs(prod_r)) < 0.05)

    W <- matrix(rnorm(m * 4), m, 4)
    cp_t <- matrix(crossprod(ts, W)[], n, 4)
    cp_r <- crossprod(ref, W)
    expect_true(max(abs(cp_t - cp_r)) / max(abs(cp_r)) < 0.05)

    message("  Test 4: binomial scaling, monomorphic variants, and guards")
    tb <- fmalloc_dosage_standardize(fmalloc_dosage(d, runtime = rt),
                                     scale = "binomial", runtime = rt)
    gb <- matrix(fmalloc_tensor_materialize(tb)[], m, n)
    p <- mu / 2
    refb <- sweep(sweep(matrix(as.numeric(imp), m, n), 2, mu, "-"),
                  2, sqrt(2 * p * (1 - p)), "/")
    expect_true(max(abs(gb - refb)) < 0.05)

    mono <- matrix(c(1, 1, 1, 1, 0, 0.5, 1.5, 2), 4L, 2L)
    gm <- matrix(fmalloc_tensor_materialize(
        fmalloc_dosage_standardize(fmalloc_dosage(mono, runtime = rt), runtime = rt))[],
        4L, 2L)
    expect_true(all(gm[, 1L] == 0))
    expect_false(anyNA(gm))

    expect_error(fmalloc_dosage_standardize(ts, runtime = rt), "already standardized")
})()
