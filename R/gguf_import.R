#' Import GGUF Tensors
#'
#' Reads some or all tensors from a GGUF file into a named list of
#' \pkg{Rfmalloc}-backed matrices/arrays, dequantizing as needed. This is a
#' thin convenience wrapper around repeated [gguf_tensor()] calls that shares
#' a single open file handle across all of them.
#'
#' @param path_or_ctx Either a path to a GGUF file, or a `"gguf_ctx"` object
#'   returned by [gguf_open()].
#' @param tensors Optional character vector of tensor names to import. If
#'   `NULL` (the default), every tensor in the file is imported.
#' @param runtime Optional `Rfmalloc` runtime handle passed through to every
#'   [gguf_tensor()] call, so all imported tensors share the same backing
#'   file. If `NULL`, `Rfmalloc`'s own default-runtime resolution is used for
#'   each tensor.
#'
#' @return A named list of `Rfmalloc`-backed matrices/arrays, one per
#'   imported tensor, in the order requested (or file order, if `tensors` is
#'   `NULL`).
#'
#' @examples
#' tmp <- tempfile(fileext = ".gguf")
#' gguf_write_tensors(tmp, list(
#'     w1 = matrix(as.double(1:12), nrow = 4, ncol = 3),
#'     w2 = matrix(as.double(1:6), nrow = 3, ncol = 2)
#' ))
#' rt <- Rfmalloc::open_fmalloc(tempfile(fileext = ".bin"))
#' mats <- gguf_import(tmp, runtime = rt)
#' prod <- mats$w1 %*% mats$w2
#' Rfmalloc::is_fmalloc_vector(prod)
#' unlink(tmp)
#'
#' @export
gguf_import <- function(path_or_ctx, tensors = NULL, runtime = NULL) {
    h <- .gguf_resolve(path_or_ctx)
    if (h$owned) {
        on.exit(gguf_close(h$ctx), add = TRUE)
    }

    available <- .Call("RC_gguf_tensor_names", h$ctx)
    if (is.null(tensors)) {
        names_to_read <- available
    } else {
        if (!is.character(tensors)) {
            stop("tensors must be a character vector of tensor names")
        }
        missing_names <- setdiff(tensors, available)
        if (length(missing_names) > 0L) {
            stop("no such tensor(s): ", paste(missing_names, collapse = ", "))
        }
        names_to_read <- tensors
    }

    result <- vector("list", length(names_to_read))
    names(result) <- names_to_read
    for (nm in names_to_read) {
        result[[nm]] <- gguf_tensor(h$ctx, nm, runtime = runtime)
    }
    result
}
