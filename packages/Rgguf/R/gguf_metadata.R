#' Read GGUF Metadata
#'
#' Reads every metadata key-value pair from a GGUF file into a named R list.
#'
#' Scalar values are converted to the closest native R type (`integer` for
#' 8/16/32-bit signed and unsigned-but-int-safe integers, `numeric` for
#' 32-bit unsigned and 64-bit integers/floats, `logical` for booleans, and
#' `character` for strings). Arrays of a supported scalar type become an R
#' vector of that type. A metadata value of a type this package does not
#' represent (there are none in the current GGUF spec, but the check is
#' defensive) is returned as `NULL` while keeping its name, rather than
#' raising an error.
#'
#' @param x Either a path to a GGUF file, or a `"gguf_ctx"` object returned by
#'   [gguf_open()].
#'
#' @return A named list of metadata values, in file order.
#'
#' @examples
#' tmp <- tempfile(fileext = ".gguf")
#' gguf_write_tensors(tmp, list(w = matrix(1:6 + 0.5, nrow = 2)),
#'     metadata = list(name = "example", version = 1))
#' gguf_metadata(tmp)
#' unlink(tmp)
#'
#' @export
gguf_metadata <- function(x) {
    h <- .gguf_resolve(x)
    if (h$owned) {
        on.exit(gguf_close(h$ctx), add = TRUE)
    }
    .Call("RC_gguf_metadata", h$ctx)
}
