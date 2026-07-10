## statgen_gwas_lin(): the fused whole-genome linear-regression GWAS scan.
## Oracle: lm(y ~ covar + g_j) fit one variant at a time, which is
## algebraically identical (Frisch-Waugh-Lovell) to the fused form under
## test, just computed the naive way. Every variant's beta/se/t/p must match
## to floating-point tolerance, not just the causal ones.
##
## The whole test runs inside one function so a single on.exit() cleans up
## the fmalloc runtime: tinytest evaluates each top-level statement in its
## own eval() frame, so a bare top-level on.exit() would fire at the end of
## its own statement, not at the end of the file.

library(Rfmalloc)
library(RfmallocStatgen)

(function() {
    set.seed(20260709)

    n <- 300L
    m <- 24L
    causal <- c(3L, 17L)

    g <- matrix(sample(0:2, n * m, replace = TRUE, prob = c(0.25, 0.5, 0.25)),
                nrow = n, ncol = m)
    storage.mode(g) <- "integer"

    covar <- cbind(age = rnorm(n, 50, 10), sex = as.numeric(rbinom(n, 1, 0.5)))

    y <- 0.9 * g[, causal[1L]] - 0.6 * g[, causal[2L]] +
        0.04 * covar[, "age"] + 0.5 * covar[, "sex"] + rnorm(n)

    tmp <- tempfile(fileext = ".bin")
    rt <- open_fmalloc(tmp, mode = "scratch", size_gb = 0.2)
    on.exit({
        cleanup_fmalloc(rt)
        unlink(tmp)
    }, add = TRUE)

    tn <- fmalloc_bed(g, runtime = rt)

    ## -- basic shape/type contract --------------------------------------

    res <- statgen_gwas_lin(tn, y, covar = covar)

    expect_true(is.data.frame(res))
    expect_equal(names(res), c("beta", "se", "t", "p", "n"))
    expect_equal(nrow(res), m)
    expect_true(all(is.finite(res$beta)))
    expect_true(all(is.finite(res$se)) && all(res$se > 0))
    expect_true(all(is.finite(res$t)))
    expect_true(all(res$p >= 0 & res$p <= 1))
    expect_equal(res$n, rep.int(n, m))

    ## -- per-variant oracle: lm(y ~ covar + g_j), tight tolerance --------

    for (j in seq_len(m)) {
        fit <- lm(y ~ covar[, "age"] + covar[, "sex"] + g[, j])
        cf <- coef(summary(fit))
        oracle <- cf[nrow(cf), ]  # g_j row: Estimate, Std. Error, t value, Pr(>|t|)

        expect_equal(res$beta[j], unname(oracle[["Estimate"]]), tolerance = 1e-6,
                     info = sprintf("beta mismatch at variant %d", j))
        expect_equal(res$se[j], unname(oracle[["Std. Error"]]), tolerance = 1e-6,
                     info = sprintf("se mismatch at variant %d", j))
        expect_equal(res$t[j], unname(oracle[["t value"]]), tolerance = 1e-6,
                     info = sprintf("t mismatch at variant %d", j))
        expect_equal(res$p[j], unname(oracle[["Pr(>|t|)"]]), tolerance = 1e-6,
                     info = sprintf("p mismatch at variant %d", j))
    }

    ## The causal variants should come through as clearly significant, and
    ## with the planted sign; a sanity check independent of the oracle.
    expect_true(res$p[causal[1L]] < 1e-6)
    expect_true(res$p[causal[2L]] < 1e-6)
    expect_true(res$beta[causal[1L]] > 0)
    expect_true(res$beta[causal[2L]] < 0)

    ## -- no covariates (intercept-only design) ---------------------------

    res_nocovar <- statgen_gwas_lin(tn, y)
    for (j in c(1L, causal)) {
        fit <- lm(y ~ g[, j])
        cf <- coef(summary(fit))
        oracle <- cf[nrow(cf), ]
        expect_equal(res_nocovar$beta[j], unname(oracle[["Estimate"]]), tolerance = 1e-6)
        expect_equal(res_nocovar$p[j], unname(oracle[["Pr(>|t|)"]]), tolerance = 1e-6)
    }

    ## -- missing genotypes: mean-imputed, matching explicit lm() imputation --

    g_na <- g
    na_idx <- cbind(sample.int(n, 15L), rep(causal[1L], 15L))
    g_na[na_idx] <- NA_integer_
    tn_na <- fmalloc_bed(g_na, runtime = rt)

    res_na <- statgen_gwas_lin(tn_na, y, covar = covar)

    g_imputed <- g_na[, causal[1L]]
    g_imputed[is.na(g_imputed)] <- mean(g_imputed, na.rm = TRUE)
    fit_na <- lm(y ~ covar[, "age"] + covar[, "sex"] + g_imputed)
    cf_na <- coef(summary(fit_na))
    oracle_na <- cf_na[nrow(cf_na), ]

    expect_equal(res_na$beta[causal[1L]], unname(oracle_na[["Estimate"]]), tolerance = 1e-6)
    expect_equal(res_na$se[causal[1L]], unname(oracle_na[["Std. Error"]]), tolerance = 1e-6)
    expect_equal(res_na$p[causal[1L]], unname(oracle_na[["Pr(>|t|)"]]), tolerance = 1e-6)

    ## Variants untouched by the injected missingness are unaffected.
    expect_equal(res_na$beta[causal[2L]], res$beta[causal[2L]], tolerance = 1e-10)

    ## -- input validation -------------------------------------------------

    expect_error(statgen_gwas_lin(tn, y[-1L]))
    expect_error(statgen_gwas_lin(tn, y, covar = covar[-1L, ]))
    expect_error(statgen_gwas_lin(tn, c(y[-1L], NA_real_)))
    expect_error(statgen_gwas_lin(matrix(1, 3, 3), y))

    ## -- dosage tensor: same oracle, continuous genotype dosages ----------

    d <- g + 0
    storage.mode(d) <- "double"
    d <- d + matrix(runif(n * m, -0.05, 0.05), n, m)  # small fractional jitter
    d[d < 0] <- 0
    d[d > 2] <- 2
    td <- fmalloc_dosage(d, runtime = rt)

    res_d <- statgen_gwas_lin(td, y, covar = covar)
    j <- causal[1L]
    fit_d <- lm(y ~ covar[, "age"] + covar[, "sex"] + d[, j])
    cf_d <- coef(summary(fit_d))
    oracle_d <- cf_d[nrow(cf_d), ]
    ## Dosage is a lossy 1-byte codec (resolution 2/254): a slightly looser
    ## tolerance than the exact 2-bit "bed" comparisons above.
    expect_equal(res_d$beta[j], unname(oracle_d[["Estimate"]]), tolerance = 1e-3)

    ## -- optional oracle: bigstatsr::big_univLinReg(), when installed -----

    if (requireNamespace("bigstatsr", quietly = TRUE)) {
        fbm <- bigstatsr::FBM(n, m, init = g)
        big_fit <- bigstatsr::big_univLinReg(fbm, y, covar.train = covar)

        expect_equal(res$beta, big_fit$estim, tolerance = 1e-6)
        expect_equal(res$t, big_fit$score, tolerance = 1e-6)
    }

    invisible(NULL)
})()
