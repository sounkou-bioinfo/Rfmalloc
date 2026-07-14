#' Open a GGUF File
#'
#' Opens a 'GGUF' model file and returns a lightweight context handle used by
#' [gguf_metadata()], [gguf_tensors()], [gguf_tensor()], and [gguf_import()].
#' Metadata and the tensor directory are parsed by Rggml's official GGUF
#' implementation. Tensor bytes are mapped read-only and unmapped
#' automatically when the returned object is garbage collected, or earlier if
#' you call `gguf_import()`/friends with a plain file path (which open and
#' close their own short-lived context internally).
#'
#' @param path Character string giving the path to a GGUF file.
#'
#' @return An object of class `"gguf_ctx"`: an external pointer to the
#'   underlying parser context, with a finalizer that closes it.
#'
#' @examples
#' tmp <- tempfile(fileext = ".gguf")
#' gguf_write_tensors(tmp, list(w = matrix(1:6 + 0.5, nrow = 2)))
#' ctx <- gguf_open(tmp)
#' gguf_tensors(ctx)
#' unlink(tmp)
#'
#' @export
gguf_open <- function(path) {
    if (!is.character(path) || length(path) != 1L || is.na(path)) {
        stop("path must be a single non-missing character string")
    }
    if (!file.exists(path)) {
        stop("no such file: ", path)
    }

    ctx <- .Call("RC_gguf_open", path)
    class(ctx) <- "gguf_ctx"
    ctx
}

#' @export
print.gguf_ctx <- function(x, ...) {
    cat("<gguf_ctx>\n")
    invisible(x)
}

# Explicitly close a gguf_ctx, releasing the memory mapping immediately
# rather than waiting for garbage collection. Not exported: callers only see
# this indirectly, through the on.exit() cleanup in the public functions that
# accept either a path or a gguf_ctx (see .gguf_resolve()).
gguf_close <- function(ctx) {
    invisible(.Call("RC_gguf_close", ctx))
}

# Resolve the "x" / "path_or_ctx" argument accepted by gguf_metadata(),
# gguf_tensors(), gguf_tensor(), and gguf_import(): either an existing
# gguf_ctx (used as-is, left open for the caller to manage) or a path
# (opened here and closed again once the caller is done with it).
.gguf_resolve <- function(x) {
    if (inherits(x, "gguf_ctx")) {
        return(list(ctx = x, owned = FALSE))
    }
    if (is.character(x) && length(x) == 1L && !is.na(x)) {
        return(list(ctx = gguf_open(x), owned = TRUE))
    }
    stop("x must be a file path (character string) or a 'gguf_ctx' object returned by gguf_open()")
}
