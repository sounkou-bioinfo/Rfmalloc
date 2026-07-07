#' (internal) Run a 'GGML' matmul through the registered C-callables
#'
#' Not exported; exists so the tinytest suite can exercise the full
#' registered C-callable path (context/tensor creation, the CPU backend,
#' and \code{ggml_mul_mat()}) exactly as a downstream \code{LinkingTo}
#' package would, driven from R.
#'
#' @param A,B Numeric matrices with the same number of rows.
#' @param zero_copy Logical; if \code{TRUE}, the tensors wrap externally
#'   owned buffers (the mmap-style zero-copy path) instead of
#'   'GGML'-managed ones.
#' @return A numeric matrix, dim \code{c(ncol(A), ncol(B))}, equal to
#'   \code{crossprod(A, B)} (i.e. \code{t(A) \%*\% B}) computed via
#'   \code{ggml_mul_mat()} on the CPU backend.
#' @keywords internal
rggml_test_mul_mat <- function(A, B, zero_copy = FALSE) {
    storage.mode(A) <- "double"
    storage.mode(B) <- "double"
    .Call("RC_rggml_test_mul_mat", A, B, isTRUE(zero_copy))
}
