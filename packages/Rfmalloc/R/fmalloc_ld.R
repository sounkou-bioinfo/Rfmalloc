#' Banded LD (correlation) matrix as a compressed fmalloc store
#'
#' Stores a banded symmetric linkage-disequilibrium (Pearson correlation)
#' matrix in fmalloc-backed, memory-mapped storage as quantized integers. This
#' is a SIBLING interface to the matmul [fmalloc_tensor] codec ABI, not an
#' instance of it (like [fmalloc_haplotypes()]): an LD matrix is read one
#' column's neighbours at a time by a Gibbs sampler or a ridge solve
#' (LDpred2), never multiplied as a decoded dense `p x p` double matrix, so
#' the store is a typed accessor with its own read API and never participates
#' in `%*%`.
#'
#' The variants are assumed position-sorted, so each column `j`'s in-window
#' neighbours form a contiguous index range `[lo_j, hi_j]`. The store keeps the
#' full symmetric band per column (both `r[j, k]` and `r[k, j]`), the diagonal
#' is `1`, and no explicit neighbour indices are stored - the row of a column's
#' `t`-th stored value is `lo_j + t`. A per-column offset table gives O(1)
#' random access to any column's neighbour run and a cache-friendly banded
#' matvec.
#'
#' Correlations are quantized to `bits`-wide integers: `r` in `[-1, 1]` becomes
#' `round(r * S)` clamped to `[-S, S]`, decoded back as `q / S`, with `S = 127`
#' for `bits = 8` (int8, resolution `~1/127`) or `S = 32767` for `bits = 16`
#' (int16, resolution `~3e-5`).
#'
#' `fmalloc_ld()` builds a store from `(i, j, x)` correlation triplets;
#' RfmallocStatgen's `statgen_snp_cor()` builds one directly from a genotype
#' tensor. Read it with [ld_ncol()], [ld_pair()] and [ld_col()].
#'
#' @param i,j Integer vectors of 1-based row/column indices of the stored
#'   correlations (COO triplets). The band of column `j` is taken as the
#'   contiguous range spanning every `i` seen for that `j` (always including
#'   the diagonal); interior gaps are stored as `0`.
#' @param x Numeric vector of correlations, `x[t] = r[i[t], j[t]]`, same length
#'   as `i` and `j`.
#' @param n_variants Number of variants (the matrix is `n_variants` x
#'   `n_variants`).
#' @param bits Quantization width, `8` (int8, the default) or `16` (int16).
#' @param window Optional integer recording the build window (informational,
#'   stored in the header); `0` if unknown.
#' @param runtime Runtime handle from [open_fmalloc()]; defaults to the runtime
#'   established by [init_fmalloc()].
#'
#' @return An `fmalloc_ld` object (a compressed, mmap-backed banded LD matrix).
#'
#' @seealso [ld_ncol()], [ld_pair()], [ld_col()]
#' @examples
#' rt <- open_fmalloc(tempfile(), size_gb = 0.1)
#' ## a 4-variant tridiagonal correlation matrix
#' i <- c(1, 2, 1, 2, 3, 2, 3, 4, 3, 4)
#' j <- c(1, 1, 2, 2, 2, 3, 3, 3, 4, 4)
#' x <- c(1, 0.5, 0.5, 1, 0.3, 0.3, 1, -0.2, -0.2, 1)
#' corr <- fmalloc_ld(i, j, x, n_variants = 4, runtime = rt)
#' ld_pair(corr, 1, 2)
#' ld_col(corr, 2)
#' cleanup_fmalloc(rt)
#' @export
fmalloc_ld <- function(i, j, x, n_variants, bits = 8L, window = 0L,
                       runtime = NULL) {
    runtime <- .fmalloc_get_runtime(runtime)
    i <- as.integer(i)
    j <- as.integer(j)
    x <- as.double(x)
    if (length(i) != length(j) || length(i) != length(x)) {
        stop("i, j and x must have the same length")
    }
    if (anyNA(i) || anyNA(j)) {
        stop("i and j must not contain missing values")
    }
    n_variants <- as.integer(n_variants)
    if (length(n_variants) != 1L || is.na(n_variants) || n_variants < 1L) {
        stop("n_variants must be a single positive integer")
    }
    bits <- as.integer(bits)
    if (!bits %in% c(8L, 16L)) {
        stop("bits must be 8 or 16")
    }
    window <- if (is.null(window)) 0L else as.integer(window)
    payload <- .Call("rfm_ld_encode_triplets_impl", i, j, x, n_variants, bits,
                     window, runtime)
    .fmalloc_ld_wrap(payload)
}

# Tag a raw ld payload as an fmalloc_ld object.
.fmalloc_ld_wrap <- function(payload) {
    class(payload) <- c("fmalloc_ld", "fmalloc")
    payload
}

#' Number of variants in a banded LD store
#'
#' @param store An [fmalloc_ld] object.
#' @return The number of variants (columns == rows) as an integer.
#' @seealso [fmalloc_ld()], [ld_pair()], [ld_col()]
#' @export
ld_ncol <- function(store) {
    if (!inherits(store, "fmalloc_ld")) {
        stop("store must be an fmalloc_ld object")
    }
    as.integer(.Call("rfm_ld_ncol_impl", store))
}

#' Correlation between two variants of a banded LD store
#'
#' Returns `r[i, j]` from a banded LD store. Pairs outside the stored band (too
#' far apart to be in each other's window) return `0`.
#'
#' @param store An [fmalloc_ld] object.
#' @param i,j 1-based variant indices.
#' @return The (quantized) correlation `r[i, j]`, or `0` if the pair is outside
#'   the band.
#' @seealso [fmalloc_ld()], [ld_col()], [ld_ncol()]
#' @export
ld_pair <- function(store, i, j) {
    if (!inherits(store, "fmalloc_ld")) {
        stop("store must be an fmalloc_ld object")
    }
    .Call("rfm_ld_pair_impl", store, as.integer(i), as.integer(j))
}

#' Neighbour run of one column of a banded LD store
#'
#' Returns column `j`'s contiguous band of correlations: the decoded values for
#' rows `lo..hi` (the window around variant `j`), where `hi - lo + 1` is the
#' band length. This is the O(1) per-column access the LDpred2 banded matvec
#' rides.
#'
#' @param store An [fmalloc_ld] object.
#' @param j 1-based column index.
#' @return A list with `lo` and `hi` (1-based inclusive row bounds of the band)
#'   and `x` (the decoded correlations for rows `lo:hi`, length `hi - lo + 1`).
#' @seealso [fmalloc_ld()], [ld_pair()], [ld_ncol()]
#' @export
ld_col <- function(store, j) {
    if (!inherits(store, "fmalloc_ld")) {
        stop("store must be an fmalloc_ld object")
    }
    res <- .Call("rfm_ld_col_impl", store, as.integer(j))
    res$lo <- as.integer(res$lo)
    res$hi <- as.integer(res$hi)
    res
}

#' @rdname fmalloc_ld
#' @param x An `fmalloc_ld` object.
#' @export
dim.fmalloc_ld <- function(x) {
    m <- as.integer(.Call("rfm_ld_ncol_impl", x))
    c(m, m)
}

#' @rdname fmalloc_ld
#' @param ... Unused.
#' @export
print.fmalloc_ld <- function(x, ...) {
    info <- .Call("rfm_ld_info_impl", x)
    m <- as.integer(info$n_variants)
    nnz <- info$nnz
    bytes <- length(unclass(x))
    cat(sprintf(
        paste0("<fmalloc_ld [%d x %d banded correlation matrix], ",
               "%.0f stored entries at %d bits, %d payload bytes",
               if (m > 0) " (%.1f bytes/variant)>\n" else ">\n"),
        m, m, nnz, as.integer(info$bits), bytes,
        if (m > 0) bytes / m else 0
    ))
    invisible(x)
}
