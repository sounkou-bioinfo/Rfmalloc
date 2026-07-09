#' Phased haplotypes as a 1-bit fmalloc store
#'
#' Encodes an `L x N` matrix of phased haplotype calls (`0`/`1`, variants in
#' rows, haplotypes in columns - the convention `kalis::CacheHaplotypes()`
#' expects for a matrix source) into fmalloc-backed, memory-mapped storage at
#' one bit per call.
#'
#' This is a SIBLING interface to the matmul [fmalloc_tensor] codec ABI, not
#' an instance of it: haplotype hidden-Markov methods (Li and Stephens local-
#' ancestry inference) are not linear algebra, so `fmalloc_haplotypes()`
#' never registers a tensor codec and the resulting object never
#' participates in `%*%`. It shares the same fmalloc storage substrate as
#' [fmalloc_bed()] and the typed tensors, with its own encode/decode pair.
#'
#' One bit per call is thirty-two times tighter than an integer `0`/`1`
#' matrix and sixty-four times tighter than the double matrix a naive
#' pipeline would build, so a haplotype panel that does not fit in RAM as
#' doubles still fits as a memory-mapped bitset.
#'
#' @param x An integer, numeric, or logical matrix of haplotype calls, values
#'   `0` or `1` only (no missing calls: phased haplotypes do not have them,
#'   and neither does `kalis`'s own matrix input).
#' @param runtime Runtime handle from [open_fmalloc()]; defaults to the
#'   runtime established by [init_fmalloc()].
#' @param ... Unused.
#'
#' @return An `fmalloc_haplotypes` object with `dim(x)`.
#'
#' @seealso [fmalloc_hap_materialize()] to decode back to a `0`/`1` matrix.
#' @examples
#' rt <- open_fmalloc(tempfile(), size_gb = 0.1)
#' h <- matrix(c(0L, 1L, 1L, 0L, 1L, 0L), nrow = 3, ncol = 2)
#' hap <- fmalloc_haplotypes(h, runtime = rt)
#' fmalloc_hap_materialize(hap, runtime = rt)
#' cleanup_fmalloc(rt)
#' @export
fmalloc_haplotypes <- function(x, runtime = NULL) {
    runtime <- .fmalloc_get_runtime(runtime)
    if (!is.matrix(x)) {
        stop("x must be a matrix of 0/1 haplotype calls (variants x haplotypes)")
    }
    if (!is.integer(x)) {
        if (is.numeric(x) || is.logical(x)) {
            storage.mode(x) <- "integer"
        } else {
            stop("x must be an integer, numeric, or logical matrix of 0/1 haplotype calls")
        }
    }
    payload <- .Call("rfm_hap_encode_impl", x, runtime)
    attr(payload, "rfm_dims") <- dim(x)
    class(payload) <- c("fmalloc_haplotypes", "fmalloc")
    payload
}

#' Materialize a bit-packed haplotype store into a 0/1 matrix
#'
#' Decodes an [fmalloc_haplotypes()] store back into an `L x N` matrix of
#' `0L`/`1L` integer calls, variants in rows and haplotypes in columns. The
#' result is itself fmalloc-backed (file-mapped storage, not an R-heap copy):
#' this is the same "decode into typed storage, not into a plain R object"
#' discipline [fmalloc_tensor_materialize()] uses for the matmul codecs.
#'
#' Passing the result straight to `kalis::CacheHaplotypes()` is the intended
#' use: kalis's matrix path only requires `is.matrix()` and `is.integer()`,
#' both true of an fmalloc-backed integer matrix, so no separate R-heap copy
#' of the decoded 0/1 matrix is needed before handing it to kalis. kalis then
#' copies the calls once more, into its own private SIMD cache layout - that
#' copy is intrinsic to kalis's architecture and would happen for any input
#' source, fmalloc-backed or not.
#'
#' @param x An `fmalloc_haplotypes` object from [fmalloc_haplotypes()].
#' @param runtime Runtime handle from [open_fmalloc()]; defaults to the
#'   runtime established by [init_fmalloc()].
#'
#' @return An fmalloc-backed integer matrix of `0L`/`1L` calls with `dim(x)`.
#'
#' @seealso [fmalloc_haplotypes()]
#' @examples
#' rt <- open_fmalloc(tempfile(), size_gb = 0.1)
#' h <- matrix(c(0L, 1L, 1L, 0L, 1L, 0L), nrow = 3, ncol = 2)
#' hap <- fmalloc_haplotypes(h, runtime = rt)
#' identical(fmalloc_hap_materialize(hap, runtime = rt)[], h)
#' cleanup_fmalloc(rt)
#' @export
fmalloc_hap_materialize <- function(x, runtime = NULL) {
    if (!inherits(x, "fmalloc_haplotypes")) {
        stop("x must be an fmalloc_haplotypes object")
    }
    runtime <- .fmalloc_get_runtime(runtime)
    ans <- .Call("rfm_hap_materialize_impl", x, runtime)
    dim(ans) <- attr(x, "rfm_dims")
    .fmalloc_apply_class(ans, type = "integer", shape = "matrix")
}

#' @rdname fmalloc_haplotypes
#' @export
dim.fmalloc_haplotypes <- function(x) {
    attr(x, "rfm_dims")
}

#' @rdname fmalloc_haplotypes
#' @export
print.fmalloc_haplotypes <- function(x, ...) {
    dims <- attr(x, "rfm_dims")
    cat(sprintf(
        "<fmalloc_haplotypes [%d variants x %d haplotypes], %d payload bytes (1 bit/call)>\n",
        dims[1L], dims[2L], length(unclass(x))
    ))
    invisible(x)
}
