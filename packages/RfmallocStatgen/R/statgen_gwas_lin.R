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
statgen_gwas_lin <- function(genotypes, y, covar = NULL) {
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

    G <- .statgen_impute_genotypes(genotypes, n, m)

    XtX_inv <- solve(crossprod(X))

    coef_y <- as.numeric(XtX_inv %*% crossprod(X, y))
    y_resid <- y - as.numeric(X %*% coef_y)
    SSy <- sum(y_resid * y_resid)

    # Frisch-Waugh-Lovell: residualizing every genotype column on X at once,
    # then reducing to sums of squares/cross-products, is algebraically
    # identical to n_variant separate lm(y ~ covar + g_j) fits but shares the
    # covariate-side work (XtX_inv, the projections) across all of them.
    XtG <- crossprod(X, G)                        # q x m
    coef_G <- XtX_inv %*% XtG                      # q x m
    SSg_resid <- colSums(G * G) - colSums(coef_G * XtG)

    cross_resid <- as.numeric(crossprod(G, y)) - as.numeric(crossprod(XtG, coef_y))

    beta <- cross_resid / SSg_resid
    RSS <- SSy - beta * beta * SSg_resid
    RSS[RSS < 0] <- 0    # floating-point guard for near-perfect fits
    se <- sqrt(RSS / df / SSg_resid)
    tstat <- beta / se
    pval <- 2 * stats::pt(-abs(tstat), df = df)

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

# Decodes a bed/dosage fmalloc_tensor to a dense n x m double matrix and
# mean-imputes missing calls per variant, the same convention
# fmalloc_bed_standardize()/fmalloc_dosage_standardize() use internally.
# This decodes the whole tensor in one pass (materialize): the scaffold's
# first cut. A follow-up can stream column panels instead, the way the
# tensor's own %*%/crossprod methods already do for panels with no missing
# data, and impute panel-by-panel (see ROADMAP.md, Tier 1).
.statgen_impute_genotypes <- function(genotypes, n, m) {
    G <- matrix(Rfmalloc::fmalloc_tensor_materialize(genotypes)[], nrow = n, ncol = m)
    has_na <- is.na(G)
    if (any(has_na)) {
        na_cols <- which(colSums(has_na) > 0L)
        means <- colMeans(G, na.rm = TRUE)
        for (j in na_cols) {
            col_na <- has_na[, j]
            G[col_na, j] <- means[j]
        }
    }
    G
}
