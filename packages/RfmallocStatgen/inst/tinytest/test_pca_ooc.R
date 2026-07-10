## statgen_pca() out-of-core path: an fmalloc genotype tensor is decomposed by
## streaming one variant-column block at a time through Rfmalloc's fused-
## standardize decode (Rfmalloc_tensor_decode), never materializing the dense
## n x m matrix. The stage-2 correctness claim is OOC == in-core: streaming the
## same standardized tensor must produce the same triplet as running the in-core
## path on that tensor fully materialized (they are the same RSVD algorithm on
## bit-identical data; only the block-summation order differs).

library(Rfmalloc)
library(RfmallocStatgen)

recon <- function(f, cols = seq_along(f$d)) {
    f$u[, cols, drop = FALSE] %*% diag(f$d[cols], length(cols)) %*%
        t(f$v[, cols, drop = FALSE])
}

(function() {
    rt <- open_fmalloc(tempfile(fileext = ".bin"), mode = "scratch", size_gb = 0.3)
    on.exit(cleanup_fmalloc(rt), add = TRUE)

    set.seed(20260710)
    n <- 260L
    m <- 150L
    ## structured genotypes: a few latent axes over a light tail (real PCA input)
    lat <- matrix(rnorm(n * 4), n, 4) %*% matrix(rnorm(4 * m), 4, m)
    g <- round(pmin(pmax(lat / 4 + 1, 0), 2))
    g[sample.int(n * m, 300L)] <- NA_integer_        # scattered missingness
    storage.mode(g) <- "integer"

    k <- 5L

    ## ================= bed tensor =====================================
    tn  <- fmalloc_bed(g, runtime = rt)
    tns <- fmalloc_bed_standardize(tn, runtime = rt)

    ## in-core oracle: materialize the standardized tensor, run in-core PCA
    Xs <- matrix(fmalloc_tensor_materialize(tns)[], n, m)
    pc_ic <- statgen_pca(Xs, k, p = 10, s = 12)

    ## out-of-core: stream the standardized tensor in small blocks (many passes)
    pc_oc <- statgen_pca(tns, k, p = 10, s = 12, block_size = 16L)

    expect_equal(names(pc_oc), c("d", "u", "v"))
    expect_equal(dim(pc_oc$u), c(n, k))
    expect_equal(dim(pc_oc$v), c(m, k))
    ## the central claim: OOC == in-core to floating point
    expect_equal(pc_oc$d, pc_ic$d, tolerance = 1e-6)
    expect_true(max(abs(recon(pc_oc) - recon(pc_ic))) < 1e-6)

    ## block size must not change the answer: 1 block, tiny blocks, default
    pc_1blk <- statgen_pca(tns, k, p = 10, s = 12, block_size = m)
    pc_tiny <- statgen_pca(tns, k, p = 10, s = 12, block_size = 7L)
    pc_def  <- statgen_pca(tns, k, p = 10, s = 12)
    expect_equal(pc_1blk$d, pc_ic$d, tolerance = 1e-6)
    expect_equal(pc_tiny$d, pc_ic$d, tolerance = 1e-6)
    expect_equal(pc_def$d,  pc_ic$d, tolerance = 1e-6)
    expect_true(max(abs(recon(pc_tiny) - recon(pc_ic))) < 1e-6)

    ## anchor to an exact oracle: the OOC singular values are the real leading
    ## singular values of the standardized matrix, not an artefact of streaming.
    ## (Only the values are checked here; genotype spectra have no clean gap, so
    ## the rank-k subspace itself is validated against the in-core path above,
    ## where both are the same RSVD and match to floating point.)
    pc3 <- statgen_pca(tns, 3L, p = 12, s = 15, block_size = 16L)
    sr <- svd(Xs)
    expect_equal(pc3$d, sr$d[1:3], tolerance = 1e-5)

    ## ================= dosage tensor ==================================
    d <- matrix(pmin(pmax(g + rnorm(n * m, 0, 0.05), 0), 2), n, m)
    d[is.na(g)] <- NA_real_
    tds <- fmalloc_dosage_standardize(fmalloc_dosage(d, runtime = rt), runtime = rt)
    Xds <- matrix(fmalloc_tensor_materialize(tds)[], n, m)
    pc_d_ic <- statgen_pca(Xds, k, p = 10, s = 12)
    pc_d_oc <- statgen_pca(tds, k, p = 10, s = 12, block_size = 16L)
    expect_equal(pc_d_oc$d, pc_d_ic$d, tolerance = 1e-6)
    expect_true(max(abs(recon(pc_d_oc) - recon(pc_d_ic))) < 1e-6)

    ## ================= input validation ===============================
    ## center/scale do not apply to a tensor (standardize the tensor instead)
    expect_error(statgen_pca(tns, k, center = TRUE), "standardize the tensor")
    expect_error(statgen_pca(tns, k, scale = TRUE), "standardize the tensor")
    ## bad block size
    expect_error(statgen_pca(tns, k, block_size = 0L), "block_size")
    ## k + s too large still caught on the tensor path
    expect_error(statgen_pca(tns, k, s = n))

    invisible(NULL)
})()
