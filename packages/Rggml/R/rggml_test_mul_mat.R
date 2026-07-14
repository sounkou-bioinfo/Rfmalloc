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
#' @param backend Which registered backend computes the product:
#'   \code{"cpu"} (default) uses \code{ggml_backend_cpu_init()};
#'   \code{"blas"} uses \code{ggml_backend_blas_init()}, which offloads the
#'   dense F32 product to whatever BLAS the R build links against.
#' @return A numeric matrix, dim \code{c(ncol(A), ncol(B))}, equal to
#'   \code{crossprod(A, B)} (i.e. \code{t(A) \%*\% B}) computed via
#'   \code{ggml_mul_mat()} on the chosen backend.
#' @keywords internal
rggml_test_mul_mat <- function(A, B, zero_copy = FALSE, backend = c("cpu", "blas")) {
    backend <- match.arg(backend)
    storage.mode(A) <- "double"
    storage.mode(B) <- "double"
    .Call("RC_rggml_test_mul_mat", A, B, isTRUE(zero_copy), identical(backend, "blas"))
}

#' (internal) Run a quantized (Q4_K) 'GGML' matmul through the C-callables
#'
#' Not exported; the quantized analogue of [rggml_test_mul_mat()] and the
#' exact operation the Rfmalloc typed-GEMM bridge performs. The weight matrix
#' \code{A} is quantized to \code{Q4_K} into a heap buffer standing in for an
#' mmap'd 'GGUF' payload, wrapped zero-copy as a \code{Q4_K} tensor, and
#' multiplied by the dense F32 activations \code{B}. \code{ggml_mul_mat()}
#' contracts each \code{Q4_K} weight row against \code{B}'s columns through
#' the runtime-SIMD-dispatched \code{ggml_vec_dot_q4_K_q8_K} (AVX2/NEON where
#' staged), quantizing \code{B} to \code{Q8_K} on the fly as at inference.
#'
#' @param A Numeric weight matrix; \code{nrow(A)} (the contracted dimension)
#'   must be a multiple of 256 (\code{QK_K}).
#' @param B Numeric activation matrix with \code{nrow(B) == nrow(A)}.
#' @param backend The GGML backend that owns the tensor buffer.
#' @return A numeric matrix, dim \code{c(ncol(A), ncol(B))}, equal to
#'   \code{crossprod(A, B)} up to q4_K/q8_K quantization error.
#' @keywords internal
rggml_test_mul_mat_q4k <- function(A, B,
                                   backend = c("cpu", "blas", "vulkan", "cuda")) {
    backend <- match.arg(backend)
    storage.mode(A) <- "double"
    storage.mode(B) <- "double"
    code <- c(cpu = 0L, blas = 1L, vulkan = 2L, cuda = 3L)[[backend]]
    .Call("RC_rggml_test_mul_mat_q4k_backend", A, B, code)
}
