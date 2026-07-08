#' Out-of-core PCA / truncated SVD for fmalloc matrices
#'
#' Principal component analysis of a large, file-backed fmalloc matrix,
#' computed from the Gram matrix: `G = X'X` via out-of-core [crossprod()], a
#' truncated eigendecomposition of the (small) `n x n` `G`, and the scores
#' `X V` via out-of-core `%*%`. Every heavy step - the Gram matrix and the
#' projection - dispatches through the pluggable matrix-multiply backend (see
#' [fmalloc_backend]), so the same call runs on CPU BLAS today and on a
#' registered GPU backend unchanged. `X` may exceed RAM: the Gram matrix and
#' projection stream column tiles with a bounded resident set.
#'
#' This is efficient when the number of features `n` is moderate (e.g. after
#' highly-variable-feature selection with [fmalloc_colVars()]); the `n x n`
#' Gram matrix and its eigendecomposition are formed in memory.
#'
#' @param X An fmalloc-backed numeric matrix (`m` observations x `n` features).
#' @param k Number of principal components to return.
#' @param center Logical; center the columns (applied implicitly as a rank-1
#'   correction to the Gram matrix and scores, so `X` is never copied).
#'
#' @return A list with prcomp-like elements: `sdev` (component standard
#'   deviations), `rotation` (`n x k` loadings), `x` (`m x k` scores), and
#'   `center` (the column means, or `FALSE`).
#'
#' @seealso [fmalloc_colVars()], [fmalloc_backend]
#' @export
fmalloc_pca <- function(X, k = 10L, center = TRUE) {
    if (!is_fmalloc_vector(X)) {
        stop("X must be an fmalloc-backed matrix")
    }
    d <- dim(X)
    if (is.null(d) || length(d) != 2L) {
        stop("X must be a matrix")
    }
    m <- d[1L]
    n <- d[2L]
    k <- min(as.integer(k), n)
    if (!is.logical(center) || length(center) != 1L || is.na(center)) {
        stop("center must be a single logical")
    }

    # Gram matrix G = X'X (n x n), out-of-core and backend-dispatched. When the
    # matrix is large enough to stream, the column sums ride along in the same
    # sweep: centering otherwise costs a second full pass over X for a reduction
    # whose inputs were already resident. In core, X is in the page cache and the
    # extra pass is not worth a special path.
    if (center && .fmalloc_crossprod_ooc_candidate(X)) {
        res <- .fmalloc_gram_ooc(X, tile_mb = getOption("Rfmalloc.ooc_tile_mb", 256),
                                 colsums = TRUE)
        G <- matrix(res$gram[], n, n)
        mu <- res$colsums / as.double(m)
    } else {
        G <- matrix(crossprod(X)[], n, n)
        mu <- if (center) as.numeric(colMeans(X)) else NULL
    }

    if (center) {
        G <- G - as.double(m) * tcrossprod(mu) # centered Gram: X'X - m mu mu'
    }

    ev <- eigen(G, symmetric = TRUE)
    ord <- seq_len(k)
    V <- ev$vectors[, ord, drop = FALSE]       # loadings (n x k)
    lambda <- pmax(ev$values[ord], 0)

    # scores = X_centered V (m x k) -- out-of-core, backend-dispatched.
    scores <- matrix((X %*% V)[], m, k)
    if (center) {
        scores <- scores - matrix(as.numeric(mu %*% V), m, k, byrow = TRUE)
    }

    list(sdev = sqrt(lambda / max(m - 1, 1)),
         rotation = V,
         x = scores,
         center = if (center) mu else FALSE)
}

#' Column / row variances of an fmalloc matrix
#'
#' Per-column (`fmalloc_colVars`) or per-row (`fmalloc_rowVars`) sample
#' variances, for highly-variable-feature selection and QC. Computed from the
#' fmalloc reduction kernels (`colMeans`/`rowMeans` of `X` and `X^2`), so the
#' result stays out-of-core-friendly and never materializes an ordinary R copy
#' of `X`.
#'
#' @param X An fmalloc-backed numeric matrix.
#' @return A numeric vector of length `ncol(X)` (`fmalloc_colVars`) or
#'   `nrow(X)` (`fmalloc_rowVars`).
#' @export
fmalloc_colVars <- function(X) {
    d <- .fmalloc_genomics_dim(X)
    m <- d[1L]
    mu <- as.numeric(colMeans(X))
    ex2 <- as.numeric(colMeans(X * X))
    (ex2 - mu * mu) * m / max(m - 1, 1)
}

#' @rdname fmalloc_colVars
#' @export
fmalloc_rowVars <- function(X) {
    d <- .fmalloc_genomics_dim(X)
    n <- d[2L]
    mu <- as.numeric(rowMeans(X))
    ex2 <- as.numeric(rowMeans(X * X))
    (ex2 - mu * mu) * n / max(n - 1, 1)
}

.fmalloc_genomics_dim <- function(X) {
    if (!is_fmalloc_vector(X)) {
        stop("X must be an fmalloc-backed matrix")
    }
    d <- dim(X)
    if (is.null(d) || length(d) != 2L) {
        stop("X must be a matrix")
    }
    d
}
