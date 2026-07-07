#' In-place (by-reference) mutation of fmalloc vectors
#'
#' Modify an fmalloc-backed atomic vector *in place*, writing straight through
#' the backing store and deliberately bypassing R's copy-on-modify. This is the
#' by-reference analogue of `x[i] <- value` / `x[] <- value`.
#'
#' Copy-on-modify is fatal at fmalloc scale: an ordinary `x[i] <- value` on a
#' larger-than-RAM or persistent vector can duplicate the whole payload. These
#' functions never copy — they update the durable store directly and return the
#' same object invisibly.
#'
#' @section Aliasing (read this):
#' Because there is no copy, all bindings to the same fmalloc vector observe the
#' change. After `y <- x; fmalloc_set(x, 1, 5)`, `y[1]` is also `5` — `x` and
#' `y` name the same backing store. This breaks R's usual value semantics *by
#' design*; for a persistent runtime it is a feature (the durable data is
#' updated). For this reason mutation is only ever done through these
#' explicitly-named functions, never a silent `[<-` method.
#'
#' Supported for fixed-width atomic vectors (logical, integer, numeric,
#' complex, raw). Indices are 1-based linear positions (column-major for
#' matrices/arrays).
#'
#' @param x An fmalloc-backed atomic vector (or matrix/array).
#' @param i Positive integer (1-based) linear indices to assign.
#' @param value For `fmalloc_set()`, a vector of length 1 (recycled) or
#'   `length(i)`. For `fmalloc_fill()`, a single scalar.
#'
#' @return `x`, invisibly, mutated in place.
#'
#' @examples
#' \dontrun{
#' rt <- open_fmalloc(tempfile(fileext = ".bin"))
#' x <- create_fmalloc_vector("numeric", 5, runtime = rt)
#' fmalloc_fill(x, 0)            # x[] <- 0, no copy
#' fmalloc_set(x, c(1, 3), 9)    # x[c(1,3)] <- 9, no copy
#' cleanup_fmalloc(rt)
#' }
#'
#' @name fmalloc_insitu
NULL

#' @rdname fmalloc_insitu
#' @export
fmalloc_set <- function(x, i, value) {
    if (!is_fmalloc_vector(x)) {
        stop("x must be an fmalloc-backed vector")
    }
    if (!is.numeric(i) || length(i) == 0L || anyNA(i)) {
        stop("i must be a non-empty vector of positive integer indices")
    }
    if (!(is.atomic(value) && !is.character(value)) || length(value) == 0L) {
        stop("value must be a non-empty atomic (non-character) vector")
    }
    invisible(.Call("rfm_set_in_place_impl", x, i, value))
}

#' @rdname fmalloc_insitu
#' @export
fmalloc_fill <- function(x, value) {
    if (!is_fmalloc_vector(x)) {
        stop("x must be an fmalloc-backed vector")
    }
    if (!(is.atomic(value) && !is.character(value)) || length(value) != 1L) {
        stop("value must be a single atomic (non-character) scalar")
    }
    invisible(.Call("rfm_fill_in_place_impl", x, value))
}
