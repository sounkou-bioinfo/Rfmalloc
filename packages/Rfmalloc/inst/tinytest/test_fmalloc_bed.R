library(tinytest)
library(Rfmalloc)

message("Testing the PLINK 1 .bed genotype codec (2 bits/genotype)...")

(function() {
    message("  Test 1: exact round-trip, including NA and a padded variant")
    tmp <- tempfile(fileext = ".bin")
    rt <- open_fmalloc(tmp, mode = "scratch", size_gb = 0.2)
    on.exit({
        cleanup_fmalloc(rt)
        unlink(tmp)
    }, add = TRUE)

    expect_true("bed" %in% fmalloc_tensor_codecs())

    set.seed(5L)
    # nrow %% 4 == 1, so every variant carries three padding genotypes in its
    # final byte: the case a flat element-to-byte mapping would get wrong.
    m <- 1001L
    n <- 40L
    g <- matrix(sample(c(0L, 1L, 2L, NA_integer_), m * n, replace = TRUE,
                       prob = c(0.4, 0.35, 0.2, 0.05)), m, n)
    tn <- fmalloc_bed(g, runtime = rt)

    expect_equal(dim(tn), c(m, n))
    expect_identical(as.numeric(fmalloc_tensor_materialize(tn)), as.numeric(g))

    # 2 bits per genotype, plus a 24-byte header. Never the 8 bytes of a double.
    bits <- 8 * length(unclass(tn)) / (m * n)
    expect_true(bits > 2 && bits < 2.02)

    message("  Test 2: dosages outside 0/1/2/NA are rejected, not silently packed")
    bad <- matrix(c(0L, 3L, 1L, 2L), 2L, 2L)
    expect_error(fmalloc_bed(bad, runtime = rt), "dosage must be")
    expect_error(fmalloc_bed(matrix(1.5, 2, 2), runtime = rt), "integer matrix")
})()

(function() {
    message("  Test 3: products decode column panels, never the whole matrix")
    tmp <- tempfile(fileext = ".bin")
    rt <- open_fmalloc(tmp, mode = "scratch", size_gb = 0.5)
    on.exit({
        cleanup_fmalloc(rt)
        unlink(tmp)
    }, add = TRUE)

    set.seed(9L)
    m <- 3000L
    n <- 250L
    k <- 4L
    # No missing values: NA would propagate through BLAS, as it should. Real
    # pipelines impute or standardize first.
    g <- matrix(sample(c(0L, 1L, 2L), m * n, replace = TRUE), m, n)
    tn <- fmalloc_bed(g, runtime = rt)
    gd <- matrix(as.numeric(g), m, n)

    # Both orientations: X %*% Omega and t(X) %*% Y are exactly the two kernels
    # a randomized SVD needs.
    V <- matrix(rnorm(n * k), n, k)
    expect_equal(matrix((tn %*% V)[], m, k), gd %*% V, tolerance = 1e-10)

    W <- matrix(rnorm(m * k), m, k)
    expect_equal(matrix(crossprod(tn, W)[], n, k), crossprod(gd, W), tolerance = 1e-10)

    # The payload stays 2 bits/genotype; only panels ever become doubles.
    expect_true(length(unclass(tn)) < m * n * 8 / 30)
})()

(function() {
    message("  Test 4: standardized decode equals scale() on the mean-imputed matrix")
    tmp <- tempfile(fileext = ".bin")
    rt <- open_fmalloc(tmp, mode = "scratch", size_gb = 0.5)
    on.exit({
        cleanup_fmalloc(rt)
        unlink(tmp)
    }, add = TRUE)

    set.seed(3L)
    m <- 2000L
    n <- 120L
    g <- matrix(sample(c(0L, 1L, 2L, NA_integer_), m * n, replace = TRUE,
                       prob = c(0.45, 0.3, 0.2, 0.05)), m, n)

    # Reference: impute each column to its non-missing mean, then scale().
    imp <- g
    mu <- colMeans(g, na.rm = TRUE)
    for (j in seq_len(n)) imp[is.na(imp[, j]), j] <- mu[j]
    ref <- scale(matrix(as.numeric(imp), m, n))

    tn <- fmalloc_bed_standardize(fmalloc_bed(g, runtime = rt), runtime = rt)
    got <- matrix(fmalloc_tensor_materialize(tn)[], m, n)

    expect_equal(got, ref, tolerance = 1e-12, check.attributes = FALSE)
    # Missing genotypes are mean-imputed, so after centering they are exactly 0.
    expect_true(all(abs(got[is.na(g)]) < 1e-12))
    # And the product a PCA would form is the standardized product.
    V <- matrix(rnorm(n * 4), n, 4)
    expect_equal(matrix((tn %*% V)[], m, 4), ref %*% V, tolerance = 1e-10)

    message("  Test 5: binomial scaling, monomorphic variants, and guards")
    tb <- fmalloc_bed_standardize(fmalloc_bed(g, runtime = rt),
                                  scale = "binomial", runtime = rt)
    gb <- matrix(fmalloc_tensor_materialize(tb)[], m, n)
    p <- mu / 2
    refb <- sweep(sweep(matrix(as.numeric(imp), m, n), 2, mu, "-"),
                  2, sqrt(2 * p * (1 - p)), "/")
    expect_equal(gb, refb, tolerance = 1e-10, check.attributes = FALSE)

    # A monomorphic variant has zero variance: it must standardize to 0, not NaN.
    mono <- matrix(c(1L, 1L, 1L, 1L, 0L, 1L, 2L, 1L), 4L, 2L)
    tm <- fmalloc_bed_standardize(fmalloc_bed(mono, runtime = rt), runtime = rt)
    gm <- matrix(fmalloc_tensor_materialize(tm)[], 4L, 2L)
    expect_true(all(gm[, 1L] == 0))
    expect_false(anyNA(gm))

    # Standardizing twice is refused, and only "bed" tensors are accepted.
    expect_error(fmalloc_bed_standardize(tn, runtime = rt), "already standardized")
})()
