#' Quantize a matrix into an Rfmalloc-backed quantized tensor
#'
#' Encodes a dense numeric matrix into a GGUF quantized block format and stores
#' the compressed payload in \pkg{Rfmalloc}-backed (file-backed, memory-mapped)
#' storage, returning an \code{fmalloc_tensor}. This is the write-side
#' counterpart to \pkg{Rgguf}'s \code{gguf_tensor(..., as = "native")}: the
#' resulting tensor keeps its quantized storage density and, when multiplied as
#' the right-hand operand of \code{dense \%*\% tensor} with the ggml backend
#' active (see [rllm_use_ggml()]), is contracted natively in quantized space by
#' GGML's SIMD-dispatched kernels without ever being decoded to double.
#'
#' The number of rows of \code{x} is the quantized (per-row) dimension and must
#' be a multiple of the codec's block size: 256 for the K-quants
#' (\code{"q2_k"}, \code{"q3_k"}, \code{"q4_k"}, \code{"q5_k"}, \code{"q6_k"})
#' and 32 for \code{"q4_0"}, \code{"q4_1"}, \code{"q5_0"}, \code{"q5_1"},
#' \code{"q8_0"}; \code{"q2_0"} uses GGML's group-64 ternary blocks.
#'
#' @param x A numeric matrix to quantize.
#' @param dtype Target quantized codec, one of \code{"q4_0"}, \code{"q4_1"},
#'   \code{"q5_0"}, \code{"q5_1"}, \code{"q8_0"}, \code{"q2_0"},
#'   \code{"q2_k"}, \code{"q3_k"}, \code{"q4_k"} (default), \code{"q5_k"},
#'   \code{"q6_k"}.
#' @param runtime Optional \pkg{Rfmalloc} runtime handle
#'   (see [Rfmalloc::open_fmalloc()]); if \code{NULL}, Rfmalloc's default
#'   runtime is used.
#'
#' @return An \code{fmalloc_tensor} of the given \code{dtype} with
#'   \code{dim(x)}.
#' @seealso [rllm_use_ggml()], [Rfmalloc::create_fmalloc_tensor()]
#' @examples
#' rt <- Rfmalloc::open_fmalloc(tempfile(fileext = ".bin"))
#' set.seed(1)
#' W <- matrix(rnorm(256 * 4, sd = 0.4), nrow = 256)  # 256 must divide nrow
#' Wt <- rllm_quantize_tensor(W, "q4_k", runtime = rt)
#' X <- matrix(rnorm(3 * 256), nrow = 3)              # 3 x 256
#' Y <- X %*% Wt                                      # native quantized product
#' dim(Y)
#' @export
rllm_quantize_tensor <- function(x, dtype = "q4_k", runtime = NULL) {
    if (!is.matrix(x) || !is.numeric(x)) {
        stop("x must be a numeric matrix")
    }
    dtype <- match.arg(dtype, c("q4_0", "q4_1", "q5_0", "q5_1", "q8_0",
                                "q2_0", "q2_k", "q3_k", "q4_k", "q5_k",
                                "q6_k"))
    storage.mode(x) <- "double"
    dims <- dim(x)
    nbytes <- .Call("RC_rllm_qtensor_nbytes", dtype, dims[1L], dims[2L],
                    PACKAGE = "Rllm")
    # Let Rfmalloc allocate the payload: it resolves runtime = NULL (and errors
    # helpfully if no runtime is open) exactly as its other constructors do.
    payload <- Rfmalloc::create_fmalloc_vector("raw", length = nbytes,
                                               runtime = runtime,
                                               zero_initialize = FALSE)
    .Call("RC_rllm_quantize_into", x, dtype, payload, PACKAGE = "Rllm")
    Rfmalloc::create_fmalloc_tensor(payload, dtype, dims)
}
