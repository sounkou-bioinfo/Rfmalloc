#' Enable or disable the ggml quantized matmul backend
#'
#' Rllm registers \pkg{Rggml} as an \pkg{Rfmalloc} codec-aware matrix-multiply
#' backend named \code{"ggml"} and selects it on load. When active, products of
#' quantized \code{fmalloc_tensor}s (types \code{"q4_0"}, \code{"q4_1"},
#' \code{"q8_0"}, \code{"q2_k"}, \code{"q4_k"}, \code{"q6_k"}) where the tensor
#' is the right-hand operand (\code{dense \%*\% tensor}) are computed by ggml in
#' quantized space, contracting each weight row through GGML's
#' SIMD-dispatched dot kernel, with the dense operand quantized on the fly.
#' Other products (the tensor on the left, non-quantized codecs) are declined
#' and fall back to Rfmalloc's decode-then-BLAS path, so results are always
#' correct regardless of the selected backend.
#'
#' Selection is Rfmalloc-scoped; base R's \code{\%*\%} is unaffected.
#'
#' @param enable If \code{TRUE} (default) select the \code{"ggml"} backend; if
#'   \code{FALSE} restore Rfmalloc's default BLAS path.
#'
#' @return Invisibly, \code{TRUE} if the ggml backend is active afterwards.
#' @seealso [rllm_quantize_tensor()]
#' @examples
#' rllm_backend_enabled()
#' rllm_use_ggml(FALSE)   # fall back to Rfmalloc's BLAS decode path
#' rllm_use_ggml(TRUE)    # re-enable ggml quantized products
#' @export
rllm_use_ggml <- function(enable = TRUE) {
    Rfmalloc::fmalloc_matmul_backend(if (isTRUE(enable)) "ggml" else "blas")
    invisible(rllm_backend_enabled())
}

#' @rdname rllm_use_ggml
#' @return `rllm_backend_enabled()` returns `TRUE` if the ggml backend is the
#'   active Rfmalloc matmul backend.
#' @export
rllm_backend_enabled <- function() {
    identical(Rfmalloc::fmalloc_matmul_backend(), "ggml")
}
