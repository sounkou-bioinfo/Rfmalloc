#' LDpred2: Bayesian polygenic scores over a banded LD matrix
#'
#' LDpred2 (Prive, Arbel & Vilhjalmsson 2020,
#' \doi{10.1093/bioinformatics/btaa1029}) reweights GWAS summary statistics
#' using an LD matrix and a point-normal (spike-and-slab) prior on the effect
#' sizes. These functions are reimplemented clean-room from the paper and run
#' over an [Rfmalloc::fmalloc_ld] banded LD store (e.g. from
#' [statgen_snp_cor()]): they read the LD matrix one column's neighbour run at a
#' time and never touch the genotypes.
#'
#' `statgen_ldpred2_inf()` is the deterministic infinitesimal model: every
#' variant is causal with effect `~ N(0, h2 / m)`, so the posterior mean solves
#' the ridge-like system \eqn{(C + \tfrac{m}{h2\,N} I)\,\beta = \hat\beta}
#' (`C` the LD matrix), here by conjugate gradient over the band. It matches
#' `bigsnpr::snp_ldpred2_inf()` to solver tolerance.
#'
#' `statgen_ldpred2()` is the LDpred2-grid Gibbs sampler for a fixed
#' polygenicity `p` and heritability `h2`: with probability `p` a variant is
#' causal with effect `~ N(0, h2 / (m p))`, else zero. It returns the
#' Rao-Blackwellized posterior-mean effects.
#'
#' `statgen_ldpred2_auto()` is LDpred2-auto: the same Gibbs sampler but with `p`
#' and `h2` estimated from the data at each iteration (the `alpha = -1`,
#' `sigma2 = h2 / (m p)` variant), returning the estimates alongside the
#' effects.
#'
#' @param corr An [Rfmalloc::fmalloc_ld] banded LD (correlation) matrix, e.g.
#'   from [statgen_snp_cor()]. Its dimension must equal `nrow(df_beta)`.
#' @param df_beta A data frame of GWAS summary statistics with columns `beta`
#'   (marginal effect), `beta_se` (its standard error, all `> 0`) and `n_eff`
#'   (effective sample size), one row per variant in `corr`'s order (bigsnpr's
#'   convention).
#' @param h2 Heritability (a positive scalar) captured by the variants.
#' @param p Proportion of causal variants for `statgen_ldpred2()` (in `(0, 1]`).
#' @param sparse Whether to seek a sparse solution (variants with posterior
#'   causal probability below `p` are set exactly to `0`). Default `FALSE`.
#' @param burn_in Number of burn-in Gibbs iterations. Default `50`
#'   (`500` for auto).
#' @param num_iter Number of iterations after burn-in whose posterior draws are
#'   averaged. Default `100` (`200` for auto).
#' @param h2_init,p_init Initial heritability and polygenicity for the auto
#'   sampler.
#' @param p_bounds Length-2 numeric bounds clamping the auto `p` estimate.
#'   Default `c(1e-5, 1)`.
#'
#' @return `statgen_ldpred2_inf()` and `statgen_ldpred2()` return a numeric
#'   vector of posterior-mean effect sizes on the allele scale, one per variant
#'   (`NA` for every variant if the Gibbs sampler diverged).
#'   `statgen_ldpred2_auto()` returns a list with `beta_est` (allele-scale
#'   effects), `postp_est` (posterior causal probabilities), `p_est`, `h2_est`
#'   (the averaged estimates) and the full `path_p_est` / `path_h2_est`.
#'
#' @seealso [statgen_snp_cor()], [Rfmalloc::fmalloc_ld]
#' @examples
#' rt <- Rfmalloc::open_fmalloc(tempfile(fileext = ".bin"), mode = "scratch",
#'                              size_gb = 0.1)
#' set.seed(1)
#' m <- 20L
#' ## a simple tridiagonal LD matrix
#' i <- j <- x <- numeric(0)
#' for (col in seq_len(m)) {
#'     rows <- max(1L, col - 1L):min(m, col + 1L)
#'     i <- c(i, rows); j <- c(j, rep(col, length(rows)))
#'     x <- c(x, ifelse(rows == col, 1, 0.3))
#' }
#' corr <- Rfmalloc::fmalloc_ld(i, j, x, n_variants = m, bits = 16L, runtime = rt)
#' df_beta <- data.frame(beta = rnorm(m, sd = 0.02), beta_se = rep(0.01, m),
#'                       n_eff = rep(10000, m))
#' beta_inf <- statgen_ldpred2_inf(corr, df_beta, h2 = 0.1)
#' head(beta_inf)
#' Rfmalloc::cleanup_fmalloc(rt)
#' @name statgen_ldpred2
#' @useDynLib RfmallocStatgen, .registration = TRUE
NULL

# Validate corr/df_beta and return the number of variants.
.ldpred2_check <- function(corr, df_beta) {
    if (!inherits(corr, "fmalloc_ld")) {
        stop("corr must be an fmalloc_ld banded LD matrix (see statgen_snp_cor())")
    }
    m <- Rfmalloc::ld_ncol(corr)
    if (!is.data.frame(df_beta) ||
        !all(c("beta", "beta_se", "n_eff") %in% names(df_beta))) {
        stop("df_beta must be a data frame with columns 'beta', 'beta_se', 'n_eff'")
    }
    if (nrow(df_beta) != m) {
        stop(sprintf("nrow(df_beta) (%d) must equal ncol(corr) (%d)",
                     nrow(df_beta), m))
    }
    if (anyNA(df_beta$beta) || anyNA(df_beta$beta_se) || anyNA(df_beta$n_eff)) {
        stop("df_beta columns must not contain missing values")
    }
    if (any(df_beta$beta_se <= 0)) {
        stop("df_beta$beta_se must be strictly positive")
    }
    m
}

# The LDpred2 scaling: scale = sqrt(N beta_se^2 + beta^2), beta_hat = beta/scale.
.ldpred2_scale <- function(df_beta) {
    sqrt(df_beta$n_eff * df_beta$beta_se^2 + df_beta$beta^2)
}

#' @rdname statgen_ldpred2
#' @export
statgen_ldpred2_inf <- function(corr, df_beta, h2) {
    m <- .ldpred2_check(corr, df_beta)
    if (length(h2) != 1L || is.na(h2) || h2 <= 0) {
        stop("h2 must be a single positive number")
    }
    N <- df_beta$n_eff
    scale <- .ldpred2_scale(df_beta)
    beta_hat <- df_beta$beta / scale
    add_to_diag <- m / (h2 * N)
    maxit <- as.integer(max(1000L, 10L * m))
    x <- .Call(C_statgen_ldpred2_inf, corr, as.double(beta_hat),
               as.double(add_to_diag), 1e-11, maxit)
    x * scale
}

#' @rdname statgen_ldpred2
#' @export
statgen_ldpred2 <- function(corr, df_beta, h2, p, sparse = FALSE,
                            burn_in = 50L, num_iter = 100L) {
    m <- .ldpred2_check(corr, df_beta)
    if (length(h2) != 1L || is.na(h2) || h2 <= 0) {
        stop("h2 must be a single positive number")
    }
    if (length(p) != 1L || is.na(p) || p <= 0 || p > 1) {
        stop("p must be a single number in (0, 1]")
    }
    N <- df_beta$n_eff
    scale <- .ldpred2_scale(df_beta)
    beta_hat <- df_beta$beta / scale
    res <- .Call(C_statgen_ldpred2_grid, corr, as.double(beta_hat), as.double(N),
                 as.double(h2), as.double(p), as.logical(sparse)[1L],
                 as.integer(burn_in), as.integer(num_iter))
    res * scale
}

#' @rdname statgen_ldpred2
#' @export
statgen_ldpred2_auto <- function(corr, df_beta, h2_init, p_init = 0.1,
                                 burn_in = 500L, num_iter = 200L,
                                 p_bounds = c(1e-5, 1)) {
    m <- .ldpred2_check(corr, df_beta)
    if (length(h2_init) != 1L || is.na(h2_init) || h2_init <= 0) {
        stop("h2_init must be a single positive number")
    }
    if (length(p_init) != 1L || is.na(p_init) || p_init <= 0 || p_init > 1) {
        stop("p_init must be a single number in (0, 1]")
    }
    if (length(p_bounds) != 2L || anyNA(p_bounds) || p_bounds[1L] <= 0 ||
        p_bounds[2L] > 1 || p_bounds[1L] > p_bounds[2L]) {
        stop("p_bounds must be an increasing length-2 vector inside (0, 1]")
    }
    N <- df_beta$n_eff
    scale <- .ldpred2_scale(df_beta)
    beta_hat <- df_beta$beta / scale
    res <- .Call(C_statgen_ldpred2_auto, corr, as.double(beta_hat), as.double(N),
                 as.double(p_init), as.double(h2_init), as.integer(burn_in),
                 as.integer(num_iter), as.double(p_bounds))
    res$beta_est <- res$beta_est * scale
    res
}
