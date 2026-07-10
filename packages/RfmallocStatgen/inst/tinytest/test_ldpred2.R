## LDpred2 over the banded fmalloc_ld store, reimplemented clean-room from
## Prive, Arbel & Vilhjalmsson (2020). Three validations per model:
##   inf : deterministic - matches a dense linear-algebra reference AND
##         bigsnpr::snp_ldpred2_inf() to solver tolerance (both on the SAME
##         quantized correlation matrix, so agreement is exact, not statistical);
##   grid: seeded self-consistency (no bigsnpr) + agrees with
##         bigsnpr::snp_ldpred2_grid() within Monte-Carlo error (corr > 0.99);
##   auto: seeded self-consistency + agrees with bigsnpr::snp_ldpred2_auto()
##         (use_MLE = FALSE) and recovers the simulated p / h2.
##
## Wrapped in one function so a single on.exit() cleans up the fmalloc runtime.

library(Rfmalloc)
library(RfmallocStatgen)

## reconstruct the exact quantized dense matrix an fmalloc_ld store holds, so
## the oracle solves the identical system (removing quantization as a source of
## disagreement for the deterministic inf comparison).
reconstruct_dense <- function(corr, m) {
    C <- matrix(0, m, m)
    for (col in seq_len(m)) {
        cc <- ld_col(corr, col)
        C[cc$lo:cc$hi, col] <- cc$x
    }
    C
}

(function() {
    set.seed(20260710)

    n <- 600L; m <- 50L; w <- 8L
    ## a PSD banded LD matrix from a small standardized genotype simulation
    G <- scale(matrix(rnorm(n * m), n, m))
    C0 <- crossprod(G) / (n - 1)
    ii <- integer(0); jj <- integer(0); xx <- numeric(0)
    for (col in seq_len(m)) {
        rows <- max(1L, col - w):min(m, col + w)
        ii <- c(ii, rows); jj <- c(jj, rep(col, length(rows))); xx <- c(xx, C0[rows, col])
    }

    tmp <- tempfile(fileext = ".bin")
    rt <- open_fmalloc(tmp, mode = "scratch", size_gb = 0.2)
    on.exit({ cleanup_fmalloc(rt); unlink(tmp) }, add = TRUE)

    ## int16 store: near-lossless, so the reconstructed Cq is essentially C0
    corr <- fmalloc_ld(ii, jj, xx, n_variants = m, bits = 16L, runtime = rt)
    Cq <- reconstruct_dense(corr, m)
    expect_equal(max(abs(Cq - t(Cq))), 0)              # symmetric band

    ## a simulated genetic architecture -> GWAS summary statistics
    N <- 20000; h2 <- 0.3; p_true <- 0.1
    mc <- rbinom(m, 1, p_true)
    gamma <- mc * rnorm(m, 0, sqrt(h2 / max(sum(mc), 1L)))
    beta_hat_std <- as.numeric(Cq %*% gamma) + rnorm(m, 0, sqrt(1 / N))
    df_beta <- data.frame(beta = beta_hat_std, beta_se = rep(1 / sqrt(N), m),
                          n_eff = rep(N, m))

    scale <- sqrt(df_beta$n_eff * df_beta$beta_se^2 + df_beta$beta^2)
    beta_hat <- df_beta$beta / scale

    ## ----------------------------------------------------------------
    ## inf: deterministic ridge solve
    ## ----------------------------------------------------------------
    mine_inf <- statgen_ldpred2_inf(corr, df_beta, h2)
    expect_equal(length(mine_inf), m)
    expect_true(all(is.finite(mine_inf)))

    ## dense linear-algebra reference on the same Cq
    d <- m / (h2 * df_beta$n_eff)
    ref_inf <- as.numeric(solve(Cq + diag(d), beta_hat)) * scale
    inf_dense_diff <- max(abs(mine_inf - ref_inf))
    message(sprintf("  inf vs dense solve():   max|diff| = %.3e", inf_dense_diff))
    expect_true(inf_dense_diff < 1e-8)

    ## different h2 -> different solution (sanity that h2 is wired through)
    expect_true(max(abs(statgen_ldpred2_inf(corr, df_beta, 0.6) - mine_inf)) > 1e-6)

    ## ----------------------------------------------------------------
    ## grid: seeded self-consistency (independent of bigsnpr)
    ## ----------------------------------------------------------------
    set.seed(11); g1 <- statgen_ldpred2(corr, df_beta, h2 = h2, p = p_true,
                                        burn_in = 50L, num_iter = 100L)
    set.seed(11); g2 <- statgen_ldpred2(corr, df_beta, h2 = h2, p = p_true,
                                        burn_in = 50L, num_iter = 100L)
    expect_identical(g1, g2)
    expect_equal(length(g1), m)
    expect_true(all(is.finite(g1)))

    ## sparse solution has exact zeros
    set.seed(11); gs <- statgen_ldpred2(corr, df_beta, h2 = h2, p = p_true,
                                        sparse = TRUE, burn_in = 50L, num_iter = 100L)
    expect_true(any(gs == 0))

    ## ----------------------------------------------------------------
    ## auto: seeded self-consistency + architecture recovery
    ## ----------------------------------------------------------------
    set.seed(21); a1 <- statgen_ldpred2_auto(corr, df_beta, h2_init = h2,
                                             burn_in = 200L, num_iter = 200L)
    set.seed(21); a2 <- statgen_ldpred2_auto(corr, df_beta, h2_init = h2,
                                             burn_in = 200L, num_iter = 200L)
    expect_identical(a1$beta_est, a2$beta_est)
    expect_identical(a1$p_est, a2$p_est)
    expect_true(is.finite(a1$p_est) && a1$p_est > 0 && a1$p_est <= 1)
    expect_true(is.finite(a1$h2_est) && a1$h2_est > 0)
    expect_equal(length(a1$postp_est), m)

    ## ----------------------------------------------------------------
    ## input validation
    ## ----------------------------------------------------------------
    expect_error(statgen_ldpred2_inf(Cq, df_beta, h2), "fmalloc_ld")
    expect_error(statgen_ldpred2_inf(corr, df_beta[, c("beta", "beta_se")], h2), "n_eff")
    expect_error(statgen_ldpred2_inf(corr, df_beta[-1L, ], h2), "must equal")
    expect_error(statgen_ldpred2_inf(corr, df_beta, -1), "positive")
    expect_error(statgen_ldpred2(corr, df_beta, h2, p = 2), "\\(0, 1\\]")

    ## ----------------------------------------------------------------
    ## optional oracle: bigsnpr, on the SAME quantized matrix Cq
    ## ----------------------------------------------------------------
    if (requireNamespace("bigsnpr", quietly = TRUE) &&
        requireNamespace("bigsparser", quietly = TRUE) &&
        requireNamespace("Matrix", quietly = TRUE)) {

        sfbm <- bigsparser::as_SFBM(methods::as(Matrix::Matrix(Cq, sparse = TRUE),
                                                "dsCMatrix"))

        ## inf: exact agreement (same system solved)
        big_inf <- bigsnpr::snp_ldpred2_inf(sfbm, df_beta, h2)
        inf_big_diff <- max(abs(mine_inf - big_inf))
        message(sprintf("  inf vs bigsnpr:         max|diff| = %.3e", inf_big_diff))
        expect_true(inf_big_diff < 1e-6)

        ## grid: within Monte-Carlo error
        set.seed(101)
        mine_g <- statgen_ldpred2(corr, df_beta, h2 = h2, p = p_true,
                                  burn_in = 200L, num_iter = 600L)
        set.seed(101)
        big_g <- bigsnpr::snp_ldpred2_grid(
            sfbm, df_beta, data.frame(p = p_true, h2 = h2, sparse = FALSE),
            burn_in = 200, num_iter = 600)[, 1]
        grid_cor <- cor(mine_g, big_g)
        grid_mad <- mean(abs(mine_g - big_g))
        message(sprintf("  grid vs bigsnpr:        cor = %.5f  mean|diff| = %.3e",
                        grid_cor, grid_mad))
        expect_true(grid_cor > 0.99)
        expect_true(grid_mad < 0.05 * sd(big_g) + 1e-6)

        ## auto: within Monte-Carlo error + parameter recovery
        set.seed(202)
        mine_a <- statgen_ldpred2_auto(corr, df_beta, h2_init = h2, p_init = 0.1,
                                       burn_in = 400L, num_iter = 400L)
        set.seed(202)
        big_a <- bigsnpr::snp_ldpred2_auto(
            sfbm, df_beta, h2_init = h2, vec_p_init = 0.1,
            burn_in = 400, num_iter = 400, use_MLE = FALSE)[[1]]
        auto_cor <- cor(mine_a$beta_est, big_a$beta_est)
        message(sprintf("  auto vs bigsnpr:        cor = %.5f  (p: %.4f vs %.4f, h2: %.4f vs %.4f)",
                        auto_cor, mine_a$p_est, big_a$p_est, mine_a$h2_est, big_a$h2_est))
        expect_true(auto_cor > 0.99)
        expect_true(abs(mine_a$p_est - big_a$p_est) < 0.05)
        expect_true(abs(mine_a$h2_est - big_a$h2_est) < 0.05)
    } else {
        message("  (bigsnpr not installed: ran inf-vs-dense + seeded self-consistency only)")
    }

    invisible(NULL)
})()
