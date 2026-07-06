#' List GGUF Tensors
#'
#' Lists the tensor directory of a GGUF file as a data frame, without reading
#' or dequantizing any tensor payload.
#'
#' @param x Either a path to a GGUF file, or a `"gguf_ctx"` object returned by
#'   [gguf_open()].
#'
#' @return A data frame with one row per tensor and columns:
#'   \describe{
#'     \item{name}{Tensor name.}
#'     \item{type}{GGUF tensor type name, e.g. `"f32"`, `"q4_k"`.}
#'     \item{n_dims}{Number of dimensions.}
#'     \item{dims}{A list column; `dims[[i]]` is an integer vector of tensor
#'       `i`'s dimensions, GGUF-order (`dim[0]` first, the fastest-varying/
#'       contiguous dimension, which [gguf_tensor()] maps to R's first,
#'       column-major dimension).}
#'     \item{n_elements}{Total number of scalar elements.}
#'     \item{nbytes}{Size of the tensor's raw (possibly quantized) on-disk
#'       representation, in bytes.}
#'     \item{offset}{Byte offset of the tensor data from the start of the
#'       file.}
#'   }
#'
#' @examples
#' tmp <- tempfile(fileext = ".gguf")
#' gguf_write_tensors(tmp, list(w = matrix(1:6 + 0.5, nrow = 2), b = 1:3 + 0.5))
#' gguf_tensors(tmp)
#' unlink(tmp)
#'
#' @export
gguf_tensors <- function(x) {
    h <- .gguf_resolve(x)
    if (h$owned) {
        on.exit(gguf_close(h$ctx), add = TRUE)
    }
    cols <- .Call("RC_gguf_tensor_table", h$ctx)
    data.frame(
        name = cols$name,
        type = cols$type,
        n_dims = cols$n_dims,
        dims = I(cols$dims),
        n_elements = cols$n_elements,
        nbytes = cols$nbytes,
        offset = cols$offset,
        stringsAsFactors = FALSE,
        row.names = NULL
    )
}

#' Read and Dequantize a GGUF Tensor
#'
#' Reads a single named tensor from a GGUF file, dequantizing it if needed,
#' into a fresh \pkg{Rfmalloc}-backed ALTREP matrix or array of doubles.
#'
#' GGUF tensor dimensions are stored with `dim[0]` as the fastest-varying
#' (contiguous) dimension. R arrays are column-major, i.e. the first
#' dimension varies fastest, so `dim[0]` maps directly onto R's first
#' dimension with no transposition needed: `dim(result) == c(dim[0], dim[1],
#' ...)`. See `inst/tinytest/test_gguf_roundtrip.R` for a test that verifies
#' this mapping by writing known matrices/arrays and reading them back.
#'
#' @param x Either a path to a GGUF file, or a `"gguf_ctx"` object returned by
#'   [gguf_open()].
#' @param name Tensor name, as it appears in [gguf_tensors()]'s `name` column.
#' @param runtime Optional `Rfmalloc` runtime handle (see
#'   [Rfmalloc::open_fmalloc()]). If `NULL`, `Rfmalloc`'s own default-runtime
#'   resolution is used.
#'
#' @return An `Rfmalloc`-backed ALTREP matrix (if the tensor has 2
#'   dimensions) or array (otherwise) of doubles, with `dim()` equal to the
#'   tensor's GGUF dimensions in `c(dim[0], dim[1], ...)` order.
#'
#' @examples
#' tmp <- tempfile(fileext = ".gguf")
#' gguf_write_tensors(tmp, list(w = matrix(1:6 + 0.5, nrow = 2)))
#' rt <- Rfmalloc::open_fmalloc(tempfile(fileext = ".bin"))
#' w <- gguf_tensor(tmp, "w", runtime = rt)
#' w
#' Rfmalloc::is_fmalloc_vector(w)
#' unlink(tmp)
#'
#' @export
gguf_tensor <- function(x, name, runtime = NULL) {
    if (!is.character(name) || length(name) != 1L || is.na(name)) {
        stop("name must be a single non-missing character string")
    }
    h <- .gguf_resolve(x)
    if (h$owned) {
        on.exit(gguf_close(h$ctx), add = TRUE)
    }

    info <- .Call("RC_gguf_tensor_info", h$ctx, name)
    if (is.null(info)) {
        stop("no such tensor: '", name, "'")
    }
    if (!isTRUE(info$supported)) {
        stop(
            "tensor '", name, "' has type '", info$type,
            "' which the vendored gguflib parser cannot dequantize"
        )
    }

    dims <- as.integer(info$dims)
    dest <- if (length(dims) == 2L) {
        Rfmalloc::create_fmalloc_matrix("numeric",
            nrow = dims[1], ncol = dims[2],
            runtime = runtime, zero_initialize = FALSE
        )
    } else {
        Rfmalloc::create_fmalloc_array("numeric",
            dim = dims,
            runtime = runtime, zero_initialize = FALSE
        )
    }

    .Call("RC_gguf_tensor_fill", h$ctx, name, dest)
    dest
}
