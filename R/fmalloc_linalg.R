#' Matrix algebra for fmalloc-backed vectors and matrices
#'
#' Provides fmalloc-aware matrix products for `%*%`, `crossprod()`, and
#' `tcrossprod()`. Results are allocated in the runtime of the first fmalloc
#' operand and returned as fmalloc-backed matrices.
#'
#' These methods use native C kernels to keep computations in package-managed
#' fmalloc storage while preserving base R behavior for matrix products.
#'
#' @param x,y Numeric, logical, or complex vectors/matrices. At least one
#'   operand must be fmalloc-backed for these methods to dispatch.
#' @param ... Unused.
#'
#' @return An fmalloc-backed numeric or complex matrix.
#'
#' @name fmalloc_linalg
NULL

# Validates without stripping the fmalloc class: `class(x) <- NULL` on a
# referenced ALTREP makes R substitute a generic wrapper object, which loses
# the fmalloc identity for vectors of length >= 64.
.fmalloc_linalg_check_operand <- function(x, arg_name) {
    if (!(is.numeric(x) || is.logical(x) || is.complex(x))) {
        stop(sprintf("%s must be a numeric, logical, or complex vector/matrix", arg_name))
    }

    x
}

.fmalloc_linalg_runtime <- function(x, y = NULL) {
    if (inherits(x, "fmalloc")) {
        return(.fmalloc_runtime_for_vector(x))
    }
    if (!is.null(y) && inherits(y, "fmalloc")) {
        return(.fmalloc_runtime_for_vector(y))
    }

    stop("at least one operand must be fmalloc-backed")
}

.fmalloc_linalg_result_type <- function(x, y = NULL) {
    if (is.complex(x) || (!is.null(y) && is.complex(y))) {
        "complex"
    } else {
        "numeric"
    }
}

.fmalloc_matmul <- function(x, y) {
    .fmalloc_linalg_runtime(x, y)
    x0 <- .fmalloc_linalg_check_operand(x, "x")
    y0 <- .fmalloc_linalg_check_operand(y, "y")

    # Large left-operand matrices auto-route to the out-of-core column-tiled
    # path so `dgemm`'s revisiting access pattern does not thrash. Elementwise
    # Ops and reductions are already single-pass streaming and are left alone.
    if (.fmalloc_matmul_ooc_candidate(x, y0)) {
        return(fmalloc_matmul_ooc(x, y0,
                                  tile_mb = getOption("Rfmalloc.ooc_tile_mb", 256)))
    }

    ans <- .Call("rfm_matrix_ops_dispatch", 0L, x0, y0)
    .fmalloc_apply_class(ans,
                         type = .fmalloc_linalg_result_type(x0, y0),
                         shape = "matrix")
}

.fmalloc_crossprod_impl <- function(x, y = NULL) {
    .fmalloc_linalg_runtime(x, y)
    x0 <- .fmalloc_linalg_check_operand(x, "x")
    y0 <- if (is.null(y)) NULL else .fmalloc_linalg_check_operand(y, "y")

    ans <- .Call("rfm_matrix_ops_dispatch", 1L, x0, y0)
    .fmalloc_apply_class(ans,
                         type = .fmalloc_linalg_result_type(x0, y0),
                         shape = "matrix")
}

.fmalloc_tcrossprod_impl <- function(x, y = NULL) {
    .fmalloc_linalg_runtime(x, y)
    x0 <- .fmalloc_linalg_check_operand(x, "x")
    y0 <- if (is.null(y)) NULL else .fmalloc_linalg_check_operand(y, "y")

    ans <- .Call("rfm_matrix_ops_dispatch", 2L, x0, y0)
    .fmalloc_apply_class(ans,
                         type = .fmalloc_linalg_result_type(x0, y0),
                         shape = "matrix")
}

#' @rdname fmalloc_linalg
#' @method %*% fmalloc
#' @export
`%*%.fmalloc` <- function(x, y) {
    .fmalloc_matmul(x, y)
}

#' @rdname fmalloc_linalg
#' @method crossprod fmalloc
#' @export
crossprod.fmalloc <- function(x, y = NULL, ...) {
    if (length(list(...)) != 0L) {
        stop("unused arguments in fmalloc crossprod method")
    }
    .fmalloc_crossprod_impl(x, y)
}

#' @rdname fmalloc_linalg
#' @method tcrossprod fmalloc
#' @export
tcrossprod.fmalloc <- function(x, y = NULL, ...) {
    if (length(list(...)) != 0L) {
        stop("unused arguments in fmalloc tcrossprod method")
    }
    .fmalloc_tcrossprod_impl(x, y)
}

#' @rdname fmalloc_linalg
#' @method matrixOps fmalloc
#' @export
matrixOps.fmalloc <- function(x, y) {
    if (identical(.Generic, "%*%")) {
        return(.fmalloc_matmul(x, y))
    }
    if (identical(.Generic, "crossprod")) {
        if (missing(y)) {
            y <- NULL
        }
        return(.fmalloc_crossprod_impl(x, y))
    }
    if (identical(.Generic, "tcrossprod")) {
        if (missing(y)) {
            y <- NULL
        }
        return(.fmalloc_tcrossprod_impl(x, y))
    }

    stop("Unsupported fmalloc matrixOps generic: ", .Generic)
}
