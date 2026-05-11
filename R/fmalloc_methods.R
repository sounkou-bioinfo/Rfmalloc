#' @importFrom methods Math2
#' @noRd
#' @exportS3Method
Ops.fmalloc <- function(e1, e2) {
    unary <- missing(e2)
    ans <- .Call("rfm_ops_dispatch", .Generic, e1, if (unary) NULL else e2, unary)

    # Propagate names for binary ops (longer operand wins)
    if (!unary) {
        n1 <- names(e1)
        n2 <- names(e2)
        if (length(n1) > 0L || length(n2) > 0L) {
            out_names <- if (length(e1) >= length(e2)) n1 else n2
            if (length(out_names) > 0L) names(ans) <- out_names
        }
    }

    out_type <- typeof(ans)
    shape <- .fmalloc_shape_class(ans)
    .fmalloc_apply_class(ans, type = .fmalloc_normalize_type(out_type), shape = shape)
}

#' @noRd
#' @exportS3Method
chooseOpsMethod.fmalloc <- function(x, y, mx, my, cl, reverse) {
    if (missing(y) || is.null(y)) return(TRUE)
    .Call("rfm_can_handle_ops_pair", x, y, .Generic)
}

#' @noRd
#' @exportS3Method
Summary.fmalloc <- function(x, ..., na.rm = FALSE) {
    x <- .fmalloc_strip_class(x)
    value <- if (identical(.Generic, "range")) {
        .fmalloc_summary_range_kernel(x, na.rm = na.rm)
    } else {
        .Primitive(.Generic)(x, ..., na.rm = na.rm)
    }

    if (length(value) == 1L) {
        return(value)
    }

    if (length(value) <= .fmalloc_reduction_result_threshold()) {
        return(value)
    }

    runtime <- .fmalloc_runtime_for_vector(x)
    .fmalloc_box_into_fmalloc(value, runtime)
}

#' @noRd
#' @exportS3Method
Math.fmalloc <- function(x, ...) {
    x <- .fmalloc_strip_class(x)
    if (length(x) == 0L) {
        value <- .Primitive(.Generic)(x, ...)
        if (length(value) == 1L) {
            return(value)
        }
        runtime <- .fmalloc_runtime_for_vector(x)
        return(.fmalloc_box_into_fmalloc(value, runtime))
    }

    runtime <- .fmalloc_runtime_for_vector(x)
    .fmalloc_math_unary_kernel(x, .Primitive(.Generic), runtime)
}

#' @noRd
#' @exportS3Method
Math2.fmalloc <- function(x, digits) {
    x <- .fmalloc_strip_class(x)
    if (length(x) == 0L) {
        value <- .Primitive(.Generic)(x, digits)
        if (length(value) == 1L) {
            return(value)
        }
        runtime <- .fmalloc_runtime_for_vector(x)
        return(.fmalloc_box_into_fmalloc(value, runtime))
    }

    runtime <- .fmalloc_runtime_for_vector(x)
    .fmalloc_math2_unary_kernel(x, .Primitive(.Generic), digits, runtime)
}

.fmalloc_warn_base_fallback <- function(op, reason) {
    warning(sprintf("fmalloc: falling back to base %s() for %s; result may be an ordinary R object", op, reason),
        call. = FALSE)
}


#' Matrix reduction helpers for fmalloc-backed matrices
#'
#' These S3 methods preserve current fmalloc behavior for matrix summary/reduction
#' operations while returning ordinary R vectors for small results.
#'
#' @details
#' These implementations keep managed execution for 2D `fmalloc` matrices with
#' `dims = 1L`.  For unsupported shapes or `dims` values (for example,
#' non-2D arrays or `dims != 1L`), the methods warn and delegate to the base R
#' implementations (`base::rowSums`, `base::colSums`, `base::rowMeans`, and
#' `base::colMeans`).
#'
#' @param x A matrix-like object.
#' @param na.rm Logical scalar controlling NA removal.
#' @param dims Numeric scalar for dimensions.
#'
#' @name fmalloc_reduction_methods
#'
#' @return The reduction result, as either an ordinary R object or a
#'   `fmalloc` vector when result length exceeds
#'   `getOption("Rfmalloc.reduce_result_length", 1e6)`.
#'
#' @rdname fmalloc_reduction_methods
#' @export
rowSums <- function(x, na.rm = FALSE, dims = 1L) {
    if (!inherits(x, "fmalloc")) {
        return(base::rowSums(x, na.rm = na.rm, dims = dims))
    }

    x_stripped <- .fmalloc_strip_class(x)
    if (!is.matrix(x_stripped) || length(dim(x_stripped)) != 2L || length(dims) != 1L || as.integer(dims) != 1L) {
        .fmalloc_warn_base_fallback("rowSums", "unsupported shape or dims argument")
        return(base::rowSums(x_stripped, na.rm = na.rm, dims = dims))
    }
    if (!is.numeric(x_stripped) && !is.logical(x_stripped) && !is.complex(x_stripped)) {
        stop("'x' must be numeric or complex")
    }

    result_len <- dim(x_stripped)[1L]
    if (result_len <= .fmalloc_reduction_result_threshold()) {
        return(.fmalloc_matrix_margin_sums(x_stripped, margin = 1L, na.rm = na.rm))
    }

    result_type <- if (is.complex(x_stripped)) "complex" else "numeric"
    runtime <- .fmalloc_runtime_for_vector(x_stripped)
    result <- create_fmalloc_vector(type = result_type, length = result_len, runtime = runtime)
    .fmalloc_matrix_margin_sums(x_stripped, margin = 1L, na.rm = na.rm, out = result)
    .fmalloc_apply_class(result, type = result_type, shape = "vector")
}

#' @rdname fmalloc_reduction_methods
#' @export
colSums <- function(x, na.rm = FALSE, dims = 1L) {
    if (!inherits(x, "fmalloc")) {
        return(base::colSums(x, na.rm = na.rm, dims = dims))
    }

    x_stripped <- .fmalloc_strip_class(x)
    if (!is.matrix(x_stripped) || length(dim(x_stripped)) != 2L || length(dims) != 1L || as.integer(dims) != 1L) {
        .fmalloc_warn_base_fallback("colSums", "unsupported shape or dims argument")
        return(base::colSums(x_stripped, na.rm = na.rm, dims = dims))
    }
    if (!is.numeric(x_stripped) && !is.logical(x_stripped) && !is.complex(x_stripped)) {
        stop("'x' must be numeric or complex")
    }

    result_len <- dim(x_stripped)[2L]
    if (result_len <= .fmalloc_reduction_result_threshold()) {
        return(.fmalloc_matrix_margin_sums(x_stripped, margin = 2L, na.rm = na.rm))
    }

    result_type <- if (is.complex(x_stripped)) "complex" else "numeric"
    runtime <- .fmalloc_runtime_for_vector(x_stripped)
    result <- create_fmalloc_vector(type = result_type, length = result_len, runtime = runtime)
    .fmalloc_matrix_margin_sums(x_stripped, margin = 2L, na.rm = na.rm, out = result)
    .fmalloc_apply_class(result, type = result_type, shape = "vector")
}

#' @rdname fmalloc_reduction_methods
#' @export
rowMeans <- function(x, na.rm = FALSE, dims = 1L) {
    if (!inherits(x, "fmalloc")) {
        return(base::rowMeans(x, na.rm = na.rm, dims = dims))
    }

    x_stripped <- .fmalloc_strip_class(x)
    if (!is.matrix(x_stripped) || length(dim(x_stripped)) != 2L || length(dims) != 1L || as.integer(dims) != 1L) {
        .fmalloc_warn_base_fallback("rowMeans", "unsupported shape or dims argument")
        return(base::rowMeans(x_stripped, na.rm = na.rm, dims = dims))
    }
    if (!is.numeric(x_stripped) && !is.logical(x_stripped) && !is.complex(x_stripped)) {
        stop("'x' must be numeric or complex")
    }

    d <- dim(x_stripped)
    result_len <- d[1L]
    if (result_len <= .fmalloc_reduction_result_threshold()) {
        return(.fmalloc_matrix_margin_means(x_stripped, margin = 1L, na.rm = na.rm))
    }

    result_type <- if (is.complex(x_stripped)) "complex" else "numeric"
    runtime <- .fmalloc_runtime_for_vector(x_stripped)
    result <- create_fmalloc_vector(type = result_type, length = result_len, runtime = runtime)
    .fmalloc_matrix_margin_means(x_stripped, margin = 1L, na.rm = na.rm, out = result)
    .fmalloc_apply_class(result, type = result_type, shape = "vector")
}

#' @rdname fmalloc_reduction_methods
#' @export
colMeans <- function(x, na.rm = FALSE, dims = 1L) {
    if (!inherits(x, "fmalloc")) {
        return(base::colMeans(x, na.rm = na.rm, dims = dims))
    }

    x_stripped <- .fmalloc_strip_class(x)
    if (!is.matrix(x_stripped) || length(dim(x_stripped)) != 2L || length(dims) != 1L || as.integer(dims) != 1L) {
        .fmalloc_warn_base_fallback("colMeans", "unsupported shape or dims argument")
        return(base::colMeans(x_stripped, na.rm = na.rm, dims = dims))
    }
    if (!is.numeric(x_stripped) && !is.logical(x_stripped) && !is.complex(x_stripped)) {
        stop("'x' must be numeric or complex")
    }

    d <- dim(x_stripped)
    result_len <- d[2L]
    if (result_len <= .fmalloc_reduction_result_threshold()) {
        return(.fmalloc_matrix_margin_means(x_stripped, margin = 2L, na.rm = na.rm))
    }

    result_type <- if (is.complex(x_stripped)) "complex" else "numeric"
    runtime <- .fmalloc_runtime_for_vector(x_stripped)
    result <- create_fmalloc_vector(type = result_type, length = result_len, runtime = runtime)
    .fmalloc_matrix_margin_means(x_stripped, margin = 2L, na.rm = na.rm, out = result)
    .fmalloc_apply_class(result, type = result_type, shape = "vector")
}

.fmalloc_summary_range_kernel <- function(x, na.rm = FALSE) {
    if (length(x) == 0L) {
        return(base::range(x, na.rm = na.rm))
    }

    if (!is.numeric(x) && !is.logical(x)) {
        return(base::range(x, na.rm = na.rm))
    }

    n <- length(x)
    if (!na.rm) {
        minimum <- x[[1L]]
        maximum <- minimum
        for (i in seq_len(n)) {
            value <- x[[i]]
            if (is.na(value)) {
                result <- c(NA, NA)
                return(if (is.logical(x)) as.integer(result) else result)
            }
            if (value < minimum) {
                minimum <- value
            }
            if (value > maximum) {
                maximum <- value
            }
        }
        result <- c(minimum, maximum)
        return(if (is.logical(x)) as.integer(result) else result)
    }

    has_value <- FALSE
    minimum <- Inf
    maximum <- -Inf

    for (i in seq_len(n)) {
        value <- x[[i]]
        if (is.na(value)) {
            next
        }
        if (!has_value) {
            minimum <- value
            maximum <- value
            has_value <- TRUE
            next
        }
        if (value < minimum) {
            minimum <- value
        }
        if (value > maximum) {
            maximum <- value
        }
    }

    if (!has_value) {
        return(c(Inf, -Inf))
    }

    result <- c(minimum, maximum)
    if (is.logical(x)) {
        return(as.integer(result))
    }
    result
}

.fmalloc_math_unary_kernel <- function(x, primitive, runtime) {
    out_type <- tryCatch(
        {
            sample <- primitive(x[[1L]])
            .fmalloc_normalize_type(typeof(sample))
        },
        error = function(err) {
            NULL
        }
    )

    if (is.null(out_type)) {
        value <- primitive(x)
        return(.fmalloc_box_into_fmalloc(value, runtime))
    }

    ans <- create_fmalloc_vector(type = out_type, length = length(x), runtime = runtime)
    for (i in seq_len(length(x))) {
        ans[[i]] <- primitive(x[[i]])
    }

    .fmalloc_apply_class(ans, type = out_type, shape = "vector")
}

.fmalloc_math2_unary_kernel <- function(x, primitive, digits, runtime) {
    out_type <- tryCatch(
        {
            sample <- primitive(x[[1L]], digits)
            .fmalloc_normalize_type(typeof(sample))
        },
        error = function(err) {
            NULL
        }
    )

    if (is.null(out_type)) {
        value <- primitive(x, digits)
        return(.fmalloc_box_into_fmalloc(value, runtime))
    }

    ans <- create_fmalloc_vector(type = out_type, length = length(x), runtime = runtime)
    for (i in seq_len(length(x))) {
        ans[[i]] <- primitive(x[[i]], digits)
    }

    .fmalloc_apply_class(ans, type = out_type, shape = "vector")
}

.fmalloc_matrix_margin_names <- function(x, margin) {
    dnames <- dimnames(x)
    if (is.null(dnames) || length(dnames) < 2L) {
        return(NULL)
    }
    if (margin == 1L) {
        dnames[[1L]]
    } else {
        dnames[[2L]]
    }
}

.fmalloc_matrix_margin_sums <- function(x, margin, na.rm = FALSE, out = NULL) {
    dims <- dim(x)
    outer_len <- if (margin == 1L) dims[1L] else dims[2L]
    inner_len <- if (margin == 1L) dims[2L] else dims[1L]
    if (outer_len == 0L) {
        out <- if (is.null(out)) {
            vector(if (is.complex(x)) "complex" else "numeric", 0L)
        } else {
            out
        }
        return(out)
    }

    if (is.null(out)) {
        out <- vector(if (is.complex(x)) "complex" else "numeric", outer_len)
    }

    is_complex <- is.complex(out)
    na_val <- if (is_complex) complex(real = NA_real_, imaginary = NA_real_) else NA_real_

    for (outer in seq_len(outer_len)) {
        idx <- if (margin == 1L) outer else (outer - 1L) * dims[1L] + 1L
        stride <- if (margin == 1L) dims[1L] else 1L
        acc <- if (is_complex) 0 + 0i else 0
        if (na.rm) {
            for (inner in seq_len(inner_len)) {
                value <- x[[idx]]
                if (!is.na(value)) {
                    acc <- acc + value
                }
                idx <- idx + stride
            }
        } else {
            has_na <- FALSE
            for (inner in seq_len(inner_len)) {
                value <- x[[idx]]
                if (is.na(value)) {
                    has_na <- TRUE
                    break
                }
                acc <- acc + value
                idx <- idx + stride
            }
            if (has_na) {
                out[[outer]] <- na_val
                next
            }
        }

        out[[outer]] <- acc
    }

    names(out) <- .fmalloc_matrix_margin_names(x, margin)
    out
}

.fmalloc_matrix_margin_means <- function(x, margin, na.rm = FALSE, out = NULL) {
    dims <- dim(x)
    outer_len <- if (margin == 1L) dims[1L] else dims[2L]
    inner_len <- if (margin == 1L) dims[2L] else dims[1L]
    if (outer_len == 0L) {
        out <- if (is.null(out)) {
            vector(if (is.complex(x)) "complex" else "numeric", 0L)
        } else {
            out
        }
        return(out)
    }

    if (is.null(out)) {
        out <- vector(if (is.complex(x)) "complex" else "numeric", outer_len)
    }

    is_complex <- is.complex(out)
    na_div0 <- if (is_complex) complex(real = NaN, imaginary = NaN) else NaN
    na_val <- if (is_complex) complex(real = NA_real_, imaginary = NA_real_) else NA_real_

    for (outer in seq_len(outer_len)) {
        idx <- if (margin == 1L) outer else (outer - 1L) * dims[1L] + 1L
        stride <- if (margin == 1L) dims[1L] else 1L

        acc <- if (is_complex) 0 + 0i else 0
        if (na.rm) {
            non_na <- 0L
            for (inner in seq_len(inner_len)) {
                value <- x[[idx]]
                if (!is.na(value)) {
                    acc <- acc + value
                    non_na <- non_na + 1L
                }
                idx <- idx + stride
            }
            if (non_na == 0L) {
                out[[outer]] <- na_div0
            } else {
                out[[outer]] <- acc / non_na
            }
            next
        }

        has_na <- FALSE
        for (inner in seq_len(inner_len)) {
            value <- x[[idx]]
            if (is.na(value)) {
                has_na <- TRUE
                break
            }
            acc <- acc + value
            idx <- idx + stride
        }

        if (has_na) {
            out[[outer]] <- na_val
        } else {
            out[[outer]] <- acc / inner_len
        }
    }

    names(out) <- .fmalloc_matrix_margin_names(x, margin)
    out
}

.fmalloc_box_into_fmalloc <- function(value, runtime) {
    if (!is.atomic(value)) {
        return(value)
    }

    type <- tryCatch(.fmalloc_normalize_type(typeof(value)), error = function(err) {
        NULL
    })
    if (is.null(type)) {
        return(value)
    }

    ans <- create_fmalloc_vector(type = type, length = length(value), runtime = runtime)
    if (length(value) > 0L) {
        ans[] <- value
    }

    shape <- .fmalloc_shape_class(value)
    ans <- .fmalloc_apply_class(ans, type = type, shape = shape)

    if (!is.null(dim(value))) {
        dim(ans) <- dim(value)
        if (!is.null(dimnames(value))) {
            dimnames(ans) <- dimnames(value)
        }
    }
    if (!is.null(names(value))) {
        names(ans) <- names(value)
    }

    ans
}
