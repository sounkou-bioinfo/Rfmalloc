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
#' `%*%` on an fmalloc matrix calls this automatically when the left operand's
#' payload reaches `getOption("Rfmalloc.ooc_threshold_gb")` (default: half of
#' physical RAM), using `getOption("Rfmalloc.ooc_tile_mb", 256)` for the tile
#' size; smaller products keep the in-core BLAS path. `crossprod()`/
#' `tcrossprod()` are not auto-routed (their output can itself exceed RAM).
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

    # Capture column names of the dense operand before stripping/coercion,
    # so the result can carry base-consistent dimnames.
    rn <- dimnames(A)[[1L]]
    cn <- if (!is.null(dim(x))) dimnames(x)[[2L]] else NULL

    x <- .fmalloc_strip_class(x)
    if (!(is.numeric(x) || is.logical(x))) {
        stop("x must be a numeric vector or matrix")
    }
    if (storage.mode(x) != "double") {
        storage.mode(x) <- "double"
    }

    ans <- .Call("rfm_matmul_ooc_impl", A, x, as.double(tile_mb) * 2^20)
    ans <- .fmalloc_apply_class(ans, type = "numeric", shape = "matrix")
    if (!is.null(rn) || !is.null(cn)) {
        dimnames(ans) <- list(rn, cn)
    }
    ans
}

#' @rdname fmalloc_matmul_ooc
#' @export
fmalloc_crossprod_ooc <- function(A, tile_mb = 256) {
    if (!is_fmalloc_vector(A)) {
        stop("A must be an fmalloc-backed matrix")
    }
    dims <- dim(A)
    if (is.null(dims) || length(dims) != 2L) {
        stop("A must be a matrix")
    }
    if (!is.double(.fmalloc_strip_class(A))) {
        stop("A must be a numeric (double) matrix")
    }
    if (!is.numeric(tile_mb) || length(tile_mb) != 1L || !is.finite(tile_mb) ||
        tile_mb <= 0) {
        stop("tile_mb must be a single positive number")
    }

    ans <- .Call("rfm_crossprod_ooc_impl", A, as.double(tile_mb) * 2^20)
    ans <- .fmalloc_apply_class(ans, type = "numeric", shape = "matrix")
    cn <- dimnames(A)[[2L]]
    if (!is.null(cn)) {
        dimnames(ans) <- list(cn, cn)
    }
    ans
}

#' @rdname fmalloc_matmul_ooc
#' @export
fmalloc_tcrossprod_ooc <- function(A, tile_mb = 256) {
    if (!is_fmalloc_vector(A)) {
        stop("A must be an fmalloc-backed matrix")
    }
    dims <- dim(A)
    if (is.null(dims) || length(dims) != 2L) {
        stop("A must be a matrix")
    }
    if (!is.double(.fmalloc_strip_class(A))) {
        stop("A must be a numeric (double) matrix")
    }
    if (!is.numeric(tile_mb) || length(tile_mb) != 1L || !is.finite(tile_mb) ||
        tile_mb <= 0) {
        stop("tile_mb must be a single positive number")
    }

    ans <- .Call("rfm_tcrossprod_ooc_impl", A, as.double(tile_mb) * 2^20)
    ans <- .fmalloc_apply_class(ans, type = "numeric", shape = "matrix")
    rn <- dimnames(A)[[1L]]
    if (!is.null(rn)) {
        dimnames(ans) <- list(rn, rn)
    }
    ans
}

# TRUE when crossprod(X) should route out-of-core: X a large fmalloc double
# matrix, single-argument (Gram matrix X'X).
.fmalloc_crossprod_ooc_candidate <- function(x) {
    if (!inherits(x, "fmalloc") || !is.double(x)) {
        return(FALSE)
    }
    xd <- dim(x)
    if (is.null(xd) || length(xd) != 2L) {
        return(FALSE)
    }
    gb <- as.double(xd[1L]) * as.double(xd[2L]) * 8 / 2^30
    gb >= .fmalloc_ooc_threshold_gb()
}

# Total physical RAM in GB, or NA when it cannot be determined. Uses a
# portable native query (POSIX sysconf, or BSD/macOS sysctl).
.fmalloc_ram_gb <- function() {
    bytes <- tryCatch(.Call("rfm_phys_ram_bytes_impl"), error = function(e) NA_real_)
    if (!is.numeric(bytes) || length(bytes) != 1L || is.na(bytes) || bytes <= 0) {
        return(NA_real_)
    }
    bytes / 2^30
}

# Payload size (GB) at or above which a matrix product auto-selects the
# out-of-core column-tiled path. Controlled by option
# `Rfmalloc.ooc_threshold_gb`; defaults to half of physical RAM, or Inf
# (never auto) when RAM is undetectable.
.fmalloc_ooc_threshold_gb <- function() {
    opt <- getOption("Rfmalloc.ooc_threshold_gb")
    if (!is.null(opt)) {
        if (!is.numeric(opt) || length(opt) != 1L || is.na(opt)) {
            stop("option 'Rfmalloc.ooc_threshold_gb' must be a single number")
        }
        return(as.double(opt))
    }
    ram <- .fmalloc_ram_gb()
    if (is.na(ram)) Inf else 0.5 * ram
}

# TRUE when `x %*% y` should route to the out-of-core path: x must be the
# left fmalloc double matrix, y a conformable real dense vector/matrix, and
# x's payload at or above the threshold. Any other shape/type returns FALSE
# and falls through to the in-core BLAS dispatch.
.fmalloc_matmul_ooc_candidate <- function(x, y0) {
    if (!inherits(x, "fmalloc") || !is.double(x)) {
        return(FALSE)
    }
    xd <- dim(x)
    if (is.null(xd) || length(xd) != 2L) {
        return(FALSE)
    }
    if (is.complex(y0)) {
        return(FALSE)
    }
    n <- xd[2L]
    yd <- dim(y0)
    if (is.null(yd)) {
        if (length(y0) != n) {
            return(FALSE)
        }
    } else if (length(yd) != 2L || yd[1L] != n) {
        return(FALSE)
    }
    gb <- as.double(xd[1L]) * as.double(xd[2L]) * 8 / 2^30
    gb >= .fmalloc_ooc_threshold_gb()
}
