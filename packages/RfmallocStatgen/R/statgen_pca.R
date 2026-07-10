#' Fast principal-component analysis by PCAone randomized SVD
#'
#' Computes the top-`k` singular triplet of a matrix with the randomized SVD
#' of Li (2023, \doi{10.1101/2022.05.25.493261}), vendored verbatim from
#' 'PCAone' (`src/pcaone/`, GPL-3) and driven here over an in-memory matrix.
#' This is the "be the pointer, don't reimplement" path: the RSVD workhorse
#' (`RsvdOpData::computeUSV`) is PCAone's own block-wise power-iteration code,
#' not a reimplementation, so results track `PCAone`/`pcaone` bit-for-bit given
#' the same seed and parameters.
#'
#' For a matrix `X` (samples x variants) the result satisfies
#' `X ~ u %*% diag(d) %*% t(v)`, matching the convention of [base::svd()]: `d`
#' are the top-`k` singular values, `u` (samples x `k`) the left singular
#' vectors, `v` (variants x `k`) the right singular vectors. With
#' `center`/`scale`, the decomposition is of the centred/scaled matrix, so `d`
#' relates to [stats::prcomp()]'s `sdev` by `d = sdev * sqrt(nrow(X) - 1)` and
#' `v` is its rotation.
#'
#' Randomized SVD is exact only in the limit; accuracy on the top `k`
#' components improves with the number of power iterations `p` and the
#' oversampling `s`, and is excellent when the spectrum decays (as genotype
#' data's does). `k + s` must not exceed `min(nrow(X), ncol(X))`.
#'
#' @param X A numeric matrix (samples x variants), or anything
#'   [base::as.matrix()] turns into one; or a two-dimensional `bed`/`dosage`
#'   [Rfmalloc::fmalloc_tensor] (samples x variants), which is decomposed
#'   *out of core*: the tensor is streamed one variant-column block at a time
#'   and the dense matrix is never materialized. To get a standardized PCA of a
#'   tensor, standardize the tensor itself first
#'   ([Rfmalloc::fmalloc_bed_standardize()] /
#'   [Rfmalloc::fmalloc_dosage_standardize()]) - standardization is a property
#'   of the tensor, fused into its decode, so `center`/`scale` do not apply to
#'   a tensor input.
#' @param k Number of principal components to return (a positive integer,
#'   `k >= 1`).
#' @param p Number of power iterations (default `7`). More iterations sharpen
#'   the top-`k` subspace at the cost of extra passes over `X`.
#' @param s Oversampling size (default `10`): the random test matrix has
#'   `k + s` columns, and `k + s <= min(dim(X))` is required.
#' @param center,scale Logical, whether to centre / scale the columns of `X`
#'   before decomposing (as [base::scale()] does). Defaults `FALSE` (a raw SVD
#'   of `X`, matching [base::svd()]).
#' @param tol Subspace-convergence tolerance for the power-iteration loop
#'   (default `1e-4`); the iteration stops early once successive estimates of
#'   the top-`k` subspace agree to `tol`.
#' @param seed Integer seed for the Gaussian random test matrix (default
#'   `112`, PCAone's own default), so runs are reproducible.
#' @param block_size For a tensor input, the number of variant columns decoded
#'   per streamed block (bounds the resident memory). `NULL` (default) picks a
#'   size that splits the variants into a handful of blocks. Ignored for a
#'   matrix input.
#'
#' @return A list with elements `d` (length-`k` singular values), `u`
#'   (`nrow(X)` x `k` left singular vectors) and `v` (`ncol(X)` x `k` right
#'   singular vectors).
#'
#' @seealso [base::svd()], [stats::prcomp()]
#' @examples
#' set.seed(1)
#' ## a matrix with a decaying spectrum, where RSVD is near-exact
#' U <- qr.Q(qr(matrix(rnorm(200 * 10), 200)))
#' V <- qr.Q(qr(matrix(rnorm(60 * 10), 60)))
#' X <- U %*% diag(5 * 0.6^(0:9)) %*% t(V)
#' pc <- statgen_pca(X, k = 3)
#' pc$d
#' svd(X)$d[1:3]
#' @export
#' @useDynLib RfmallocStatgen, .registration = TRUE
statgen_pca <- function(X, k, p = 7L, s = 10L, center = FALSE, scale = FALSE,
                        tol = 1e-4, seed = 112L, block_size = NULL) {
    if (inherits(X, "fmalloc_tensor")) {
        return(.statgen_pca_ooc(X, k, p = p, s = s, center = center,
                                scale = scale, tol = tol, seed = seed,
                                block_size = block_size))
    }

    X <- as.matrix(X)
    if (!is.numeric(X)) {
        stop("X must be a numeric matrix")
    }
    storage.mode(X) <- "double"
    n <- nrow(X)
    m <- ncol(X)
    dims <- .statgen_pca_check_dims(k, p, s, n, m)
    k <- dims$k; p <- dims$p; s <- dims$s

    # method 1 = PCAone's single-pass sSVD (NormalRsvdOpData). The windowed
    # winSVD (method 2) is the out-of-core algorithm: in-core its fixed-window
    # block bookkeeping is only valid when the variant count aligns to its
    # window count, so it is not exposed on this dense-matrix entry point.
    method <- 1L

    if (isTRUE(center) || isTRUE(scale)) {
        X <- scale(X, center = center, scale = scale)
        attr(X, "scaled:center") <- NULL
        attr(X, "scaled:scale") <- NULL
    }

    .Call(C_statgen_pca_incore, X, k, p, s, method, as.double(tol), as.integer(seed))
}

# Shared parameter validation for both entry points; returns coerced integers.
.statgen_pca_check_dims <- function(k, p, s, n, m) {
    k <- as.integer(k)
    if (length(k) != 1L || is.na(k) || k < 1L) {
        stop("k must be a single positive integer")
    }
    p <- as.integer(p)
    s <- as.integer(s)
    if (is.na(p) || p < 1L) {
        stop("p (power iterations) must be a positive integer")
    }
    if (is.na(s) || s < 0L) {
        stop("s (oversampling) must be a non-negative integer")
    }
    if (k + s > min(n, m)) {
        stop(sprintf("k + s (%d) must not exceed min(nrow, ncol) = %d",
                     k + s, min(n, m)))
    }
    list(k = k, p = p, s = s)
}

# Out-of-core PCA of a 2-D bed/dosage fmalloc tensor: stream one variant-column
# block at a time through Rfmalloc's fused-standardize decode (see the C-callable
# Rfmalloc_tensor_decode). The full n x m double matrix is never materialized.
.statgen_pca_ooc <- function(X, k, p, s, center, scale, tol, seed, block_size) {
    if (!(attr(X, "rfm_dtype") %in% c("bed", "dosage"))) {
        stop("out-of-core PCA supports 'bed' and 'dosage' fmalloc_tensors")
    }
    dims <- dim(X)
    if (length(dims) != 2L) {
        stop("tensor must be 2-dimensional (samples x variants)")
    }
    if (isTRUE(center) || isTRUE(scale)) {
        stop("for an fmalloc_tensor, standardize the tensor itself with ",
             "fmalloc_bed_standardize()/fmalloc_dosage_standardize() rather ",
             "than passing center/scale")
    }
    n <- dims[1L]
    m <- dims[2L]
    chk <- .statgen_pca_check_dims(k, p, s, n, m)

    if (is.null(block_size)) {
        # a handful of blocks by default, so streaming is actually exercised
        # while keeping per-block decode work reasonable
        block_size <- max(1L, as.integer(ceiling(m / 8)))
    } else {
        block_size <- as.integer(block_size)
        if (length(block_size) != 1L || is.na(block_size) || block_size < 1L) {
            stop("block_size must be a single positive integer")
        }
    }

    .Call(C_statgen_pca_ooc, X, as.integer(n), as.integer(m),
          chk$k, chk$p, chk$s, as.double(tol), as.integer(seed), block_size)
}
