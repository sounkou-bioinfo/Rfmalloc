#' Write a Minimal GGUF File
#'
#' Writes a named list of numeric vectors/matrices/arrays to a GGUF file as
#' 32-bit floating point (`f32`) tensors, with optional simple metadata. This
#' is a minimal writer, primarily meant to build small GGUF fixtures for this
#' package's own round-trip tests without shipping binary test fixtures, but
#' it is exported since it is also useful on its own for producing test
#' fixtures for other GGUF consumers.
#'
#' Tensor dimensions are taken from each object's `dim()` (or its `length()`
#' for a plain vector, written as a 1-dimensional tensor) and stored in GGUF
#' `dim[0]`-fastest-varying order directly from R's column-major storage, so
#' `gguf_tensor()`/`gguf_import()` read back exactly the matrix/array you
#' wrote (see `inst/tinytest/test_gguf_roundtrip.R`).
#'
#' Existing files at `path` are silently overwritten.
#'
#' @param path Character string giving the output file path.
#' @param tensors A non-empty named list of numeric vectors, matrices, or
#'   arrays. Names become tensor names and must be unique and non-empty.
#' @param metadata A named list of metadata key-value pairs to write. Each
#'   value must be a single (length-1), non-missing string or numeric value;
#'   numeric values are written as 64-bit floats (`FLOAT64`). Defaults to
#'   `list()` (no metadata).
#'
#' @return `path`, invisibly.
#'
#' @examples
#' tmp <- tempfile(fileext = ".gguf")
#' gguf_write_tensors(tmp,
#'     tensors = list(weight = matrix(1:6 + 0.5, nrow = 2)),
#'     metadata = list(name = "example-model", version = 1)
#' )
#' gguf_tensors(tmp)
#' unlink(tmp)
#'
#' @export
gguf_write_tensors <- function(path, tensors, metadata = list()) {
    if (!is.character(path) || length(path) != 1L || is.na(path)) {
        stop("path must be a single non-missing character string")
    }
    if (!is.list(tensors) || length(tensors) == 0L) {
        stop("tensors must be a non-empty named list of numeric vectors/matrices/arrays")
    }
    tensor_names <- names(tensors)
    if (is.null(tensor_names) || any(!nzchar(tensor_names)) || anyDuplicated(tensor_names)) {
        stop("tensors must be a named list with unique, non-empty names")
    }

    tensor_values <- vector("list", length(tensors))
    names(tensor_values) <- tensor_names
    for (nm in tensor_names) {
        value <- tensors[[nm]]
        if (!is.numeric(value)) {
            stop("tensor '", nm, "' is not numeric")
        }
        storage.mode(value) <- "double"
        tensor_values[[nm]] <- value
    }

    if (!is.list(metadata)) {
        stop("metadata must be a list")
    }
    if (length(metadata) > 0L) {
        metadata_names <- names(metadata)
        if (is.null(metadata_names) || any(!nzchar(metadata_names)) || anyDuplicated(metadata_names)) {
            stop("metadata must be a named list with unique, non-empty names")
        }
        for (nm in metadata_names) {
            value <- metadata[[nm]]
            valid <- (is.character(value) || is.numeric(value)) && length(value) == 1L && !is.na(value)
            if (!valid) {
                stop("metadata entry '", nm, "' must be a single non-missing string or numeric value")
            }
        }
    }

    .Call("RC_gguf_write", path, tensor_values, metadata)
    invisible(path)
}
