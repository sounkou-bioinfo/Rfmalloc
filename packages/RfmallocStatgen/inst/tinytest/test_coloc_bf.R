## statgen_coloc_bf(): the matrix (GEMM) reinterpretation of coloc.bf_bf must be
## numerically identical to the consecutive, per-pair vector calculation, and
## must recover colocalisation (H4) vs distinct-causal (H3) correctly.

## ---- a small, independent reference: coloc's combine.abf, one pair at a time
lse <- function(x) { m <- max(x); if (!is.finite(m)) return(m); m + log(sum(exp(x - m))) }
ldiff1 <- function(a, b) if (a <= b) -Inf else a + log1p(-exp(b - a))
combine_abf_pair <- function(l1, l2, p1, p2, p12) {
    s1 <- lse(l1); s2 <- lse(l2); cc <- lse(l1 + l2)
    lH <- c(0, log(p1) + s1, log(p2) + s2,
            log(p1) + log(p2) + ldiff1(s1 + s2, cc), log(p12) + cc)
    exp(lH - lse(lH))
}
naive <- function(bf1, bf2, p1 = 1e-4, p2 = 1e-4, p12 = 1e-5) {
    res <- NULL
    for (b in seq_len(nrow(bf2))) for (a in seq_len(nrow(bf1)))
        res <- rbind(res, combine_abf_pair(bf1[a, ], bf2[b, ], p1, p2, p12))
    res
}

## ---- synthetic signals ----
set.seed(1); n <- 400L
mk <- function(peak, height = 20, width = 2) {
    z <- height * exp(-((seq_len(n) - peak)^2) / (2 * width^2))
    W <- 0.2; r <- W / (W + 1)
    0.5 * log(1 - r) + (z^2) * r / 2 + rnorm(n, 0, 0.02)
}
bf1 <- rbind(mk(120), mk(300))   # trait 1: signals at 120, 300
bf2 <- rbind(mk(120), mk(320))   # trait 2: signals at 120 (shared), 320 (distinct)

## ---- 1. GEMM form is identical to the naive per-pair form ----
got <- statgen_coloc_bf(bf1, bf2, backend = "cpu")
gm <- as.matrix(got[, c("PP0", "PP1", "PP2", "PP3", "PP4")])
expect_true(max(abs(gm - naive(bf1, bf2))) < 1e-10,
    info = "matrix (GEMM) coloc == consecutive vector coloc, to machine precision")

## ---- 2. valid posteriors ----
expect_true(max(abs(rowSums(gm) - 1)) < 1e-10, info = "each pair's PP0..PP4 sum to 1")
expect_true(all(gm >= -1e-12 & gm <= 1 + 1e-12), info = "posteriors in [0,1]")

## ---- 3. recovers colocalisation vs distinct causals ----
pp4 <- function(a, b) got$PP4[got$signal1 == a & got$signal2 == b]
pp3 <- function(a, b) got$PP3[got$signal1 == a & got$signal2 == b]
expect_true(pp4(1, 1) > 0.99, info = "shared signal (t1@120, t2@120) -> PP4 ~ 1")
expect_true(pp3(2, 2) > 0.99, info = "distinct signals (t1@300, t2@320) -> PP3 ~ 1")
expect_true(pp4(1, 2) < 0.01 && pp4(2, 1) < 0.01,
    info = "non-overlapping signal pairs do not colocalise")

## ---- 4. trim_by_posterior overlap flags low-overlap H4 ----
tr <- statgen_coloc_bf(bf1, bf2, trim = TRUE, overlap_min = 0.5)
ov <- function(a, b) tr$overlap[tr$signal1 == a & tr$signal2 == b]
keep <- function(a, b) tr$keep[tr$signal1 == a & tr$signal2 == b]
expect_true(ov(1, 1) > 0.9 && isTRUE(keep(1, 1)), info = "shared signal: high overlap, kept")
expect_true(ov(2, 2) < 0.1 && isFALSE(keep(2, 2)), info = "distinct signal: no overlap, trimmed")

## ---- 5. shape + argument checks ----
expect_equal(nrow(got), nrow(bf1) * nrow(bf2))
expect_error(statgen_coloc_bf(bf1, bf2[, 1:10]), "same number of SNP")

## ---- 6. blas backend (fp32) matches cpu when Rggml is present ----
if (requireNamespace("Rggml", quietly = TRUE)) {
    gb <- statgen_coloc_bf(bf1, bf2, backend = "blas")
    gbm <- as.matrix(gb[, c("PP0", "PP1", "PP2", "PP3", "PP4")])
    expect_true(max(abs(gbm - gm)) < 1e-3,
        info = "single-precision GEMM backend agrees with the double-precision CPU path")
}
