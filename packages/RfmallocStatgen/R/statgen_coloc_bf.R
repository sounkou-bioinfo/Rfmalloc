#' Colocalisation of two traits' signals by Bayes factors (all pairs at once)
#'
#' A matrix reinterpretation of coloc's \code{coloc.bf_bf}: instead of testing
#' one signal of trait 1 against one signal of trait 2 with vector operations
#' (and looping), the shared-causal (H4) evidence for *every* pair of signals is
#' a single matrix multiply. The consecutive per-pair calculation and this
#' matrix form are algebraically identical (see Details); the payoff is that the
#' one hot kernel is a GEMM, so it rides any backend Rggml offers - CPU,
#' OpenBLAS, or a Vulkan GPU (including via dzn) - and, over Rfmalloc's
#' out-of-core panel-GEMM, signal sets larger than device/host memory. This is
#' the same idea as gpu-coloc (Torch), minus the CUDA/Metal-only lock-in.
#'
#' @details
#' For per-SNP log approximate Bayes factors \eqn{l_1} (a signal of trait 1) and
#' \eqn{l_2} (a signal of trait 2) over the same \eqn{n} aligned SNPs, coloc's
#' \code{combine.abf} gives the five hypotheses' evidence from three summaries:
#' \eqn{s_1=\mathrm{lse}(l_1)}, \eqn{s_2=\mathrm{lse}(l_2)} and
#' \eqn{c=\mathrm{lse}(l_1+l_2)} (\eqn{\mathrm{lse}}=\code{logSumExp}):
#' \deqn{lH_1=\log p_1+s_1,\; lH_2=\log p_2+s_2,}
#' \deqn{lH_3=\log p_1+\log p_2+\mathrm{logdiff}(s_1+s_2,\,c),\; lH_4=\log p_{12}+c,}
#' and \eqn{PP_k=\exp(lH_k-\mathrm{lse}(0,lH_1,..,lH_4))}.
#' Only \eqn{c} couples the two signals. Stacking trait 1's signals as rows of
#' \code{bf1} (\eqn{S_1\times n}) and trait 2's as \code{bf2} (\eqn{S_2\times n}),
#' the whole \eqn{S_1\times S_2} matrix of \eqn{c} is, with per-row maxima
#' \eqn{m_1,m_2} and \eqn{E_1=\exp(bf1-m_1)}, \eqn{E_2=\exp(bf2-m_2)}:
#' \deqn{c[a,b]=m_1[a]+m_2[b]+\log\left(E_1 E_2^\top\right)[a,b].}
#' The \eqn{E_1 E_2^\top} is the GEMM; the per-row max subtraction is the
#' log-sum-exp stabilisation, so it is exact in exact arithmetic and safe in
#' floating point. \eqn{s_1,s_2} are per-row log-sum-exps (cheap); everything
#' after \eqn{c} is elementwise over the \eqn{S_1\times S_2} grid.
#'
#' @param bf1,bf2 numeric matrices of per-SNP log ABFs, rows = signals, columns
#'   = SNPs. Both must have the same number of columns and their columns must be
#'   the same, aligned SNPs.
#' @param p1,p2,p12 coloc priors: P(SNP causal for trait 1 only / trait 2 only /
#'   both).
#' @param backend one of \code{"cpu"} (base R, double precision), \code{"blas"}
#'   or \code{"vulkan"} (dispatch the GEMM to Rggml's backend; single precision).
#'   Non-CPU backends fall back to \code{"cpu"} with a message when Rggml or a
#'   device is unavailable.
#' @param trim logical; if \code{TRUE}, also return the per-pair posterior
#'   overlap (\eqn{\sum_i \min(pp_{1,a,i}, pp_{2,b,i})}, one minus the total
#'   variation distance of the two signals' within-signal posteriors) and mark
#'   pairs below \code{overlap_min}. This is the analogue of coloc's
#'   \code{trim_by_posterior}: pairs whose strongly-associated SNPs barely
#'   overlap get an unreliable H4 and are flagged.
#' @param overlap_min overlap threshold for \code{trim}.
#'
#' @return a data.frame, one row per signal pair, with columns \code{signal1},
#'   \code{signal2}, \code{nsnps}, \code{PP0}..\code{PP4}, and (when
#'   \code{trim}) \code{overlap} and \code{keep}.
#' @export
statgen_coloc_bf <- function(bf1, bf2, p1 = 1e-4, p2 = 1e-4, p12 = 1e-5,
                             backend = c("cpu", "blas", "vulkan"),
                             trim = FALSE, overlap_min = 0.5) {
    backend <- match.arg(backend)
    if (!is.matrix(bf1)) bf1 <- rbind(bf1)
    if (!is.matrix(bf2)) bf2 <- rbind(bf2)
    storage.mode(bf1) <- "double"
    storage.mode(bf2) <- "double"
    if (ncol(bf1) != ncol(bf2)) {
        stop("bf1 and bf2 must have the same number of SNP columns (aligned SNPs)")
    }
    n <- ncol(bf1)
    S1 <- nrow(bf1)
    S2 <- nrow(bf2)

    ## per-row log-sum-exp of a matrix, stable
    row_lse <- function(M) {
        m <- apply(M, 1L, max)
        m + log(rowSums(exp(M - m)))
    }
    s1 <- row_lse(bf1) # length S1
    s2 <- row_lse(bf2) # length S2

    ## the coupling term c[a,b] = lse_i(bf1[a,i] + bf2[b,i]) via one GEMM
    m1 <- apply(bf1, 1L, max)
    m2 <- apply(bf2, 1L, max)
    E1 <- exp(bf1 - m1) # S1 x n, entries in (0, 1]
    E2 <- exp(bf2 - m2) # S2 x n
    ## want E1 %*% t(E2)  (S1 x S2); crossprod(X, Y) = t(X) %*% Y, so feed
    ## X = t(E1) (n x S1), Y = t(E2) (n x S2) -> t(t(E1)) %*% t(E2) = E1 %*% t(E2)
    dot <- .coloc_gemm(t(E1), t(E2), backend)
    cc <- outer(m1, m2, "+") + log(dot) # S1 x S2, = c[a,b]

    ## combine.abf, vectorised over the S1 x S2 grid
    lp1 <- log(p1); lp2 <- log(p2); lp12 <- log(p12)
    lH1 <- matrix(lp1 + s1, S1, S2)               # depends on a only
    lH2 <- matrix(lp2 + s2, S1, S2, byrow = TRUE) # depends on b only
    s1s2 <- outer(s1, s2, "+")
    lH3 <- lp1 + lp2 + .logdiff(s1s2, cc)         # distinct causals
    lH4 <- lp12 + cc
    lH0 <- matrix(0, S1, S2)
    ## denominator = lse over the 5 hypotheses, elementwise
    denom <- .lse5(lH0, lH1, lH2, lH3, lH4)
    PP0 <- exp(lH0 - denom); PP1 <- exp(lH1 - denom); PP2 <- exp(lH2 - denom)
    PP3 <- exp(lH3 - denom); PP4 <- exp(lH4 - denom)

    out <- data.frame(
        signal1 = rep(seq_len(S1), times = S2),
        signal2 = rep(seq_len(S2), each = S1),
        nsnps = n,
        PP0 = as.vector(PP0), PP1 = as.vector(PP1), PP2 = as.vector(PP2),
        PP3 = as.vector(PP3), PP4 = as.vector(PP4)
    )
    if (trim) {
        ## within-signal posteriors (softmax of each bf row), then per-pair
        ## overlap = sum_i min(pp1[a,i], pp2[b,i]).
        pp1 <- exp(bf1 - s1) # S1 x n rows sum to 1
        pp2 <- exp(bf2 - s2) # S2 x n
        ov <- matrix(0, S1, S2)
        for (b in seq_len(S2)) {
            ov[, b] <- rowSums(pmin(pp1, matrix(pp2[b, ], S1, n, byrow = TRUE)))
        }
        out$overlap <- as.vector(ov)
        out$keep <- out$overlap >= overlap_min
    }
    out
}

## log(exp(a) - exp(b)) elementwise, assuming a >= b; a==b -> -Inf; a==-Inf -> -Inf
.logdiff <- function(a, b) {
    d <- a + log1p(-exp(b - a))
    d[!is.finite(a)] <- -Inf
    d[a <= b] <- -Inf # guard tiny negatives from rounding
    d
}

## elementwise log-sum-exp of five conformable matrices, stable
.lse5 <- function(m0, m1, m2, m3, m4) {
    mx <- pmax(m0, m1, m2, m3, m4)
    mx + log(exp(m0 - mx) + exp(m1 - mx) + exp(m2 - mx) + exp(m3 - mx) + exp(m4 - mx))
}

## crossprod(A, B) = t(A) %*% B on the chosen backend, always returns double.
## Non-CPU backends dispatch the one hot kernel to Rggml's backend GEMM (which
## itself runs on CPU/OpenBLAS/Vulkan); Rggml is a Suggests, so degrade to the
## base-R GEMM whenever it (or a device) is unavailable rather than erroring.
.coloc_gemm <- function(A, B, backend) {
    if (backend == "cpu") return(crossprod(A, B))
    if (!requireNamespace("Rggml", quietly = TRUE)) {
        message("statgen_coloc_bf: Rggml not installed; using the CPU GEMM")
        return(crossprod(A, B))
    }
    res <- tryCatch(Rggml::rggml_mul_mat(A, B, backend = backend),
                    error = function(e) e)
    if (inherits(res, "error")) {
        message("statgen_coloc_bf: ", backend, " backend unavailable (",
                conditionMessage(res), "); using the CPU GEMM")
        return(crossprod(A, B))
    }
    res
}
