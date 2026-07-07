#' Vendored 'GGML' library version
#'
#' Returns the version string reported by the vendored 'GGML' library at
#' runtime (\code{ggml_version()}), resolved through Rggml's own registered
#' C-callable rather than a compile-time constant, so it always reflects
#' the code that was actually built.
#'
#' @return A length-1 character vector.
#' @export
#' @examples
#' ggml_version()
ggml_version <- function() {
    .Call("RC_rggml_version")
}
