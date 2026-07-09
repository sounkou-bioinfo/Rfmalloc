## End-to-end demo, exercised as a test: .pgen -> rpgen_dosage() ->
## Rfmalloc::fmalloc_dosage_standardize() -> Rfmalloc::fmalloc_pca(), with no
## dense double copy of the genotype matrix at any point (only pgenlib's own
## decode in C, and fmalloc's bounded column-panel decodes inside crossprod()/
## the projection). See inst/examples/pgen_pca.R for the narrated version this
## mirrors.

library(Rpgen)

(function() {
    pgen_path <- system.file("extdata", "chr21_phase3_start.pgen", package = "Rpgen")
    info <- rpgen_info(pgen_path)

    tmp <- tempfile(fileext = ".bin")
    rt <- Rfmalloc::open_fmalloc(tmp, mode = "scratch", size_gb = 0.5)
    on.exit({
        Rfmalloc::cleanup_fmalloc(rt)
        unlink(tmp)
    }, add = TRUE)

    dosages <- rpgen_dosage(pgen_path, runtime = rt)
    expect_true(inherits(dosages, "fmalloc_tensor"))
    expect_equal(dim(dosages), c(info$n_sample, info$n_variant))

    standardized <- Rfmalloc::fmalloc_dosage_standardize(dosages, runtime = rt)
    expect_true(inherits(standardized, "fmalloc_tensor"))

    k <- 5L
    pca <- Rfmalloc::fmalloc_pca(standardized, k = k, center = FALSE)

    expect_true(is.list(pca))
    expect_equal(names(pca), c("sdev", "rotation", "x", "center"))
    expect_equal(length(pca$sdev), k)
    expect_equal(dim(pca$rotation), c(info$n_variant, k))
    expect_equal(dim(pca$x), c(info$n_sample, k))

    ## Sane PCs: finite, non-negative variances in non-increasing order (the
    ## defining property of a PCA's component ordering), and a first PC that
    ## actually explains more variance than a uniformly-random direction
    ## would (the fixture is real 1000 Genomes chr21 data, so real population
    ## structure/LD should dominate over the last few components at least).
    expect_true(all(is.finite(pca$sdev)))
    expect_true(all(pca$sdev >= 0))
    expect_true(all(diff(pca$sdev) <= 1e-8))
    expect_true(pca$sdev[1L] > pca$sdev[k])
    expect_true(all(is.finite(pca$x)))
    expect_true(all(is.finite(pca$rotation)))

    ## Loadings are orthonormal (a property of the eigendecomposition PCA is
    ## built from, independent of the input data): V'V = I_k.
    vtv <- crossprod(pca$rotation)
    expect_true(max(abs(vtv - diag(k))) < 1e-6)
})()
