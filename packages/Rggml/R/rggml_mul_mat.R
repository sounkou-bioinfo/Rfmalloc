#' Matrix product on a chosen 'GGML' backend
#'
#' Computes \code{crossprod(A, B)} (that is \code{t(A) \%*\% B}) through
#' 'GGML' on the requested backend, using the backend-agnostic residency path:
#' the operands and result are allocated in one of the backend's own buffers,
#' the inputs uploaded, the product computed, and the result downloaded. For a
#' GPU backend the tensors live in device memory, so this is the entry point a
#' caller uses to run a dense single-precision GEMM on the GPU.
#'
#' @param A,B Numeric matrices with the same number of rows (the contracted
#'   dimension). Computed in single precision on every backend.
#' @param backend One of \code{"cpu"} (\code{ggml_backend_cpu_init()}),
#'   \code{"blas"} (offloads the dense F32 product to whatever BLAS the R build
#'   links against), or \code{"vulkan"} (a Vulkan device; requires Rggml built
#'   with \code{--with-vulkan} and a visible device, see
#'   [rggml_vulkan_info()]). Errors if the requested backend is unavailable.
#' @return A numeric matrix, dim \code{c(ncol(A), ncol(B))}, equal to
#'   \code{crossprod(A, B)} up to single-precision rounding.
#' @seealso [rggml_vulkan_info()], [rggml_cpu_info()]
#' @examples
#' A <- matrix(rnorm(12), 4, 3)
#' B <- matrix(rnorm(8), 4, 2)
#' rggml_mul_mat(A, B)               # == crossprod(A, B) to fp32
#' @export
rggml_mul_mat <- function(A, B, backend = c("cpu", "blas", "vulkan")) {
    backend <- match.arg(backend)
    if (!is.matrix(A) || !is.matrix(B)) stop("A and B must be matrices")
    storage.mode(A) <- "double"
    storage.mode(B) <- "double"
    code <- c(cpu = 0L, blas = 1L, vulkan = 2L)[[backend]]
    .Call("RC_rggml_test_mul_mat_backend", A, B, code)
}
