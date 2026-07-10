## statgen_pca(): PCAone randomized SVD driven over an in-memory matrix.
##
## Oracles: base::svd() and stats::prcomp() (exact), and pcaone::pcaone() when
## installed (the same PCAone RSVD, from its own R package). Randomized SVD is
## exact only in the limit, so the tight comparisons use matrices with a
## decaying spectrum - a clean spectral gap at every k, where enough power
## iterations drive the top-k triplet to machine precision. Genotype PCA input
## has exactly this shape (a few strong population axes over a light tail).

library(RfmallocStatgen)

## A matrix with orthonormal factors and a geometrically decaying spectrum:
## X = U diag(20 * decay^(0:(r-1))) V^T, U (n x r) and V (m x r) orthonormal.
mk_decaying <- function(n, m, r, decay, seed) {
    set.seed(seed)
    U <- qr.Q(qr(matrix(rnorm(n * r), n, r)))
    V <- qr.Q(qr(matrix(rnorm(m * r), m, r)))
    U %*% diag(20 * decay^(0:(r - 1))) %*% t(V)
}

## Sign-free way to compare two rank-k SVDs: the rank-k reconstruction
## u diag(d) v^T is unique when d[k] > d[k+1], so compare that, not u/v (whose
## signs and within-degenerate-block rotation are free).
recon <- function(fit, cols = seq_along(fit$d)) {
    fit$u[, cols, drop = FALSE] %*% diag(fit$d[cols], length(cols)) %*%
        t(fit$v[, cols, drop = FALSE])
}

(function() {
    ## -- shape / contract -------------------------------------------------
    X <- mk_decaying(300, 120, 15, 0.6, seed = 1)
    k <- 5L
    pc <- statgen_pca(X, k, p = 10, s = 12)

    expect_true(is.list(pc))
    expect_equal(names(pc), c("d", "u", "v"))
    expect_equal(length(pc$d), k)
    expect_equal(dim(pc$u), c(nrow(X), k))
    expect_equal(dim(pc$v), c(ncol(X), k))
    expect_true(all(is.finite(pc$d)) && all(pc$d >= 0))
    expect_true(all(diff(pc$d) <= 0))  # singular values descending

    ## -- oracle 1: base::svd(), tight ------------------------------------
    sr <- svd(X)
    expect_equal(pc$d, sr$d[1:k], tolerance = 1e-6)
    ## rank-k reconstruction (sign-free) matches the exact rank-k truncation
    rec_svd <- sr$u[, 1:k] %*% diag(sr$d[1:k]) %*% t(sr$v[, 1:k])
    expect_true(max(abs(recon(pc) - rec_svd)) < 1e-6)
    ## u, v are (near) orthonormal
    expect_equal(crossprod(pc$u), diag(k), tolerance = 1e-6)
    expect_equal(crossprod(pc$v), diag(k), tolerance = 1e-6)

    ## -- oracle 2: pcaone::pcaone() (same RSVD, its own package) ----------
    if (requireNamespace("pcaone", quietly = TRUE)) {
        po <- pcaone::pcaone(X, k = k, p = 10, q = 12, method = "alg1")
        expect_equal(sort(pc$d, decreasing = TRUE),
                     sort(po$d, decreasing = TRUE), tolerance = 1e-6)
        rec_po <- po$u %*% diag(as.numeric(po$d)) %*% t(po$v)
        expect_true(max(abs(recon(pc) - rec_po)) < 1e-5)
    }

    ## -- oracle 3: stats::prcomp(), centred -------------------------------
    ## 4 strong axes + a negligible floor: top-3 is cleanly separated, so the
    ## centred decomposition matches prcomp to machine precision.
    Xc <- mk_decaying(400, 80, 4, 0.5, seed = 9) +
        matrix(rnorm(400 * 80, sd = 1e-9), 400, 80)
    kk <- 3L
    pcc <- statgen_pca(Xc, kk, p = 12, s = 15, center = TRUE)
    pr <- prcomp(Xc, center = TRUE, scale. = FALSE)
    ## singular values of the centred matrix relate to sdev by sqrt(n - 1)
    expect_equal(pcc$d, pr$sdev[1:kk] * sqrt(nrow(Xc) - 1), tolerance = 1e-6)
    ## scores (u diag d) equal prcomp's x up to a per-component sign
    scores <- pcc$u %*% diag(pcc$d)
    for (j in seq_len(kk)) {
        expect_true(abs(cor(scores[, j], pr$x[, j])) > 1 - 1e-8)
    }
    ## v is prcomp's rotation up to sign
    expect_equal(recon(pcc),
                 pr$x[, 1:kk] %*% t(pr$rotation[, 1:kk]), tolerance = 1e-6)

    ## -- scale = TRUE agrees with svd(scale(X)) --------------------------
    pcs <- statgen_pca(X, k, p = 10, s = 12, center = TRUE, scale = TRUE)
    Xs <- scale(X, center = TRUE, scale = TRUE)
    srs <- svd(Xs)
    expect_equal(pcs$d, srs$d[1:k], tolerance = 1e-6)

    ## -- determinism & seed sensitivity ----------------------------------
    expect_identical(statgen_pca(X, k), statgen_pca(X, k))
    ## a different seed gives the same singular values (they do not depend on
    ## the random test matrix once converged), but is a genuinely different run
    pc_seed <- statgen_pca(X, k, p = 10, s = 12, seed = 999L)
    expect_equal(pc$d, pc_seed$d, tolerance = 1e-6)

    ## -- input validation -------------------------------------------------
    expect_error(statgen_pca(X, 0))                 # k < 1
    expect_error(statgen_pca(X, 5, s = ncol(X)))    # k + s > min(dim)
    expect_error(statgen_pca(X, 5, p = 0))          # p < 1
    expect_error(statgen_pca(matrix("a", 5, 5), 2)) # non-numeric

    invisible(NULL)
})()
