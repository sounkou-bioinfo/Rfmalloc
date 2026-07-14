#' Univariate linear-regression GWAS over an fmalloc genotype tensor
#'
#' Regresses a continuous phenotype on every variant in a `bed` or `dosage`
#' [Rfmalloc::fmalloc_tensor], one variant at a time, adjusting for an
#' optional covariate design. This is the fused form
#' `bigstatsr::big_univLinReg()` uses (Prive et al. 2018,
#' \doi{10.1093/bioinformatics/bty185}), not a loop of per-variant `lm()`
#' calls: the phenotype and every genotype column are residualized on the
#' covariate design `[1, covar]` once (the Frisch-Waugh-Lovell theorem), and
#' every variant's slope, standard error, t-statistic, and p-value then
#' follow from a handful of crossprod reductions shared across all variants.
#'
#' Missing genotype calls are mean-imputed per variant before regression,
#' matching the convention [Rfmalloc::fmalloc_bed_standardize()] and
#' [Rfmalloc::fmalloc_dosage_standardize()] use to bake mean-imputation into
#' their codecs' own decode. `y` and `covar` must not contain missing
#' values: drop or impute those samples before calling this function.
#'
#' @param genotypes A `"bed"` or `"dosage"` [Rfmalloc::fmalloc_tensor]
#'   (samples x variants; see [Rfmalloc::fmalloc_bed()] /
#'   [Rfmalloc::fmalloc_dosage()]).
#' @param y A numeric phenotype vector, one value per sample
#'   (`nrow(genotypes)`).
#' @param covar `NULL` (the default), or a numeric vector/matrix/data frame
#'   of covariates, one row per sample. An intercept column is prepended
#'   automatically; do not include one.
#' @param block_size Number of variant columns decoded at once. `NULL` chooses
#'   a block holding at most roughly 64 MiB of decoded doubles. This affects
#'   memory use and speed, not the result.
#'
#' @return A data frame with one row per variant (in tensor column order)
#'   and columns `beta` (the regression slope on the variant), `se` (its
#'   standard error), `t` (`beta / se`), `p` (the two-sided p-value from the
#'   `t` distribution on `n - rank([1, covar]) - 1` residual degrees of
#'   freedom), and `n` (samples used; currently the same for every variant,
#'   since missing genotypes are imputed rather than dropped).
#'
#' @seealso [Rfmalloc::fmalloc_bed()], [Rfmalloc::fmalloc_dosage()]
#' @examples
#' rt <- Rfmalloc::open_fmalloc(tempfile(fileext = ".bin"), mode = "scratch", size_gb = 0.1)
#' set.seed(1)
#' n <- 200L
#' m <- 10L
#' g <- matrix(sample(0:2, n * m, replace = TRUE), nrow = n, ncol = m)
#' storage.mode(g) <- "integer"
#' y <- g[, 1L] * 0.5 + rnorm(n)
#' tn <- Rfmalloc::fmalloc_bed(g, runtime = rt)
#' res <- statgen_gwas_lin(tn, y)
#' head(res)
#' Rfmalloc::cleanup_fmalloc(rt)
#' @export
statgen_gwas_lin <- function(genotypes, y, covar = NULL, block_size = NULL) {
    if (!inherits(genotypes, "fmalloc_tensor") ||
        !(attr(genotypes, "rfm_dtype") %in% c("bed", "dosage"))) {
        stop("genotypes must be a 'bed' or 'dosage' fmalloc_tensor (see ",
             "fmalloc_bed()/fmalloc_dosage())")
    }
    dims <- dim(genotypes)
    if (length(dims) != 2L) {
        stop("genotypes must be a 2-dimensional tensor (samples x variants)")
    }
    n <- dims[1L]
    m <- dims[2L]

    y <- as.numeric(y)
    if (length(y) != n) {
        stop(sprintf("length(y) (%d) must equal nrow(genotypes) (%d)", length(y), n))
    }
    if (anyNA(y)) {
        stop("y must not contain missing values")
    }

    X <- .statgen_design_matrix(n, covar)
    q <- ncol(X)
    rank_x <- qr(X)$rank
    if (rank_x < q) {
        stop("covariate design [1, covar] is rank-deficient")
    }

    df <- n - rank_x - 1L
    if (df < 1L) {
        stop("no residual degrees of freedom left (n too small for the covariate design)")
    }

    XtX_inv <- solve(crossprod(X))

    coef_y <- as.numeric(XtX_inv %*% crossprod(X, y))
    y_resid <- y - as.numeric(X %*% coef_y)
    SSy <- sum(y_resid * y_resid)

    block_size <- .statgen_gwas_block_size(block_size, n, m)
    beta <- se <- tstat <- pval <- rep.int(NA_real_, m)

    # Frisch-Waugh-Lovell, one bounded variant panel at a time. The covariate
    # side is shared across every panel; only n * block_size decoded doubles
    # are resident. Missing calls are mean-imputed inside that same panel.
    for (j0 in seq.int(1L, m, by = block_size)) {
        jb <- min(block_size, m - j0 + 1L)
        jj <- j0:(j0 + jb - 1L)
        G <- .Call(C_statgen_decode_panel, genotypes, as.integer(n),
                   as.integer(j0 - 1L), as.integer(jb))

        observed <- colSums(!is.na(G))
        means <- colSums(G, na.rm = TRUE) / observed
        for (k in which(observed < n & observed > 0L)) {
            miss <- is.na(G[, k])
            G[miss, k] <- means[k]
        }
        # An all-missing column has no estimable effect. Zeroing its temporary
        # values keeps the shared BLAS reductions finite; its result stays NA.
        if (any(observed == 0L)) {
            G[, observed == 0L] <- 0
        }

        XtG <- crossprod(X, G)
        coef_G <- XtX_inv %*% XtG
        sumsq <- colSums(G * G)
        SSg_resid <- sumsq - colSums(coef_G * XtG)
        cross_resid <- as.numeric(crossprod(G, y)) -
            as.numeric(crossprod(XtG, coef_y))

        tol <- 64 * .Machine$double.eps * pmax(1, sumsq)
        estimable <- observed > 0L & is.finite(SSg_resid) & SSg_resid > tol
        if (any(estimable)) {
            b <- cross_resid[estimable] / SSg_resid[estimable]
            rss <- SSy - b * b * SSg_resid[estimable]
            rss[rss < 0] <- 0
            s <- sqrt(rss / df / SSg_resid[estimable])
            tt <- b / s

            out <- jj[estimable]
            beta[out] <- b
            se[out] <- s
            tstat[out] <- tt
            pval[out] <- 2 * stats::pt(-abs(tt), df = df)
        }
    }

    data.frame(
        beta = beta,
        se = se,
        t = tstat,
        p = pval,
        n = rep.int(n, m)
    )
}

# Builds cbind(1, covar), validated: no missing values, correct row count.
.statgen_design_matrix <- function(n, covar) {
    if (is.null(covar)) {
        return(matrix(1, nrow = n, ncol = 1L))
    }
    if (is.data.frame(covar)) {
        covar <- as.matrix(covar)
    }
    if (is.null(dim(covar))) {
        if (length(covar) != n) {
            stop(sprintf("length(covar) (%d) must equal nrow(genotypes) (%d)", length(covar), n))
        }
        covar <- matrix(covar, nrow = n, ncol = 1L)
    } else if (nrow(covar) != n) {
        stop(sprintf("nrow(covar) (%d) must equal nrow(genotypes) (%d)", nrow(covar), n))
    }
    storage.mode(covar) <- "double"
    if (anyNA(covar)) {
        stop("covar must not contain missing values")
    }
    cbind(1, covar)
}

.statgen_gwas_block_size <- function(block_size, n, m) {
    if (is.null(block_size)) {
        block_size <- max(1, floor((64 * 2^20) / (8 * n)))
    }
    block_size <- as.integer(block_size)
    if (length(block_size) != 1L || is.na(block_size) || block_size < 1L) {
        stop("block_size must be a single positive integer")
    }
    min(block_size, m)
}
