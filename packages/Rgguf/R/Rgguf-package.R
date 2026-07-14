#' Rgguf: Official GGUF parsing and typed storage views
#'
#' Rgguf reads 'GGUF' model files, the file format used by the 'llama.cpp'
#' project to store large language model tensors and metadata, and exposes
#' their tensors as decoded \pkg{Rfmalloc} arrays, owned native-codec copies,
#' or borrowed read-only views. Parsing, writing, and quantized decoding use
#' the official GGUF and type-traits implementation carried by the sibling
#' \pkg{Rggml} package.
#'
#' @section Main Functions:
#' \describe{
#'   \item{\code{\link{gguf_open}}}{Open a GGUF file and return a context handle.}
#'   \item{\code{\link{gguf_metadata}}}{Read all metadata key-value pairs.}
#'   \item{\code{\link{gguf_tensors}}}{List the tensor directory as a data frame.}
#'   \item{\code{\link{gguf_tensor}}}{Read, copy, or borrow one tensor.}
#'   \item{\code{\link{gguf_import}}}{Apply the same storage choice to several tensors.}
#'   \item{\code{\link{gguf_write_tensors}}}{Write a minimal GGUF file (F32 tensors), mainly for round-trip testing.}
#' }
#'
#' @section Design:
#' Allocation happens on the R side: tensor destinations are created with
#' \code{Rfmalloc::create_fmalloc_matrix()}/\code{create_fmalloc_array()},
#' which returns a properly classed, file-backed ALTREP object with
#' \pkg{Rfmalloc}'s full \code{Ops}/matrix-product dispatch already working.
#' Native code only ever fills that destination in place (dequantizing as it
#' goes); it never allocates R vectors of tensor size itself.
#'
#' @section Storage:
#' \itemize{
#'   \item GGUF files are mapped read-only. Metadata and tensor geometry are
#'         parsed once by Rggml's official GGUF implementation.
#'   \item Numeric import decodes through GGML in bounded chunks into the
#'         fmalloc destination. Native import copies encoded bytes into owned
#'         fmalloc storage. View import borrows the original mapped span.
#' }
#'
#' @keywords internal
#' @useDynLib Rgguf, .registration = TRUE
"_PACKAGE"
