#' Out-of-core matrix product for fmalloc matrices larger than RAM
#'
#' Computes `A %*% x` where `A` is a large column-major fmalloc-backed double
#' matrix living in the backing file and `x` is an ordinary numeric vector or
#' matrix. `A` is consumed one contiguous column tile at a time: each tile is
#' multiplied into the accumulator with BLAS `dgemm`, then its pages are
#' released with `madvise(MADV_DONTNEED)`, so the resident set stays bounded by
#' `tile_mb` rather than the size of `A`. This lets `A` exceed physical RAM.
#'
#' The backing storage is advised `MADV_SEQUENTIAL` so the kernel reads ahead.
#' The result is an fmalloc-backed matrix allocated in `A`'s runtime.
#'
#' @param A An fmalloc-backed double matrix (`m x n`).
#' @param x A numeric vector of length `n`, or a numeric matrix (`n x k`).
#' @param tile_mb Target resident megabytes per column tile of `A`. Larger
#'   tiles amortize BLAS overhead; smaller tiles bound peak memory more
#'   tightly. Defaults to 256.
#'
#' @return An fmalloc-backed double matrix (`m x k`), equal to `A %*% x`.
#'
#' @examples
#' \dontrun{
#' rt <- open_fmalloc(tempfile(fileext = ".bin"), size_gb = 8)
#' A <- create_fmalloc_matrix("numeric", nrow = 100000, ncol = 20000, runtime = rt)
#' # ... fill A ...
#' y <- fmalloc_matmul_ooc(A, rnorm(20000), tile_mb = 128)
#' cleanup_fmalloc(rt)
#' }
#'
#' @export
fmalloc_matmul_ooc <- function(A, x, tile_mb = 256) {
    if (!is_fmalloc_vector(A)) {
        stop("A must be an fmalloc-backed matrix")
    }
    dims <- dim(A)
    if (is.null(dims) || length(dims) != 2L) {
        stop("A must be a matrix")
    }
    if (!is.numeric(tile_mb) || length(tile_mb) != 1L || !is.finite(tile_mb) ||
        tile_mb <= 0) {
        stop("tile_mb must be a single positive number")
    }

    x <- .fmalloc_strip_class(x)
    if (!(is.numeric(x) || is.logical(x))) {
        stop("x must be a numeric vector or matrix")
    }
    if (storage.mode(x) != "double") {
        storage.mode(x) <- "double"
    }

    ans <- .Call("rfm_matmul_ooc_impl", A, x, as.double(tile_mb) * 2^20)
    .fmalloc_apply_class(ans, type = "numeric", shape = "matrix")
}
