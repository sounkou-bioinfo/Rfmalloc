#' Typed fmalloc tensors
#'
#' A typed fmalloc tensor is an fmalloc raw vector holding a matrix payload in
#' a foreign element encoding (`"f32"`, `"f16"`, `"bf16"`, or any codec
#' registered by another package through the `Rfmalloc_register_tensor_codec`
#' C-callable), plus dimension and dtype tags. Matrix products against dense
#' double operands decode the payload in bounded, block-aligned column panels
#' that are streamed through BLAS `dgemm`, so the double representation of the
#' full tensor is never materialized at once.
#'
#' `create_fmalloc_tensor()` tags an existing fmalloc raw payload.
#' `fmalloc_tensor_materialize()` decodes the whole tensor into an fmalloc
#' double matrix. `fmalloc_tensor_codecs()` lists registered codec names, and
#' `fmalloc_tensor_dtype()` returns a tensor's dtype tag.
#'
#' @param payload An fmalloc raw vector holding the encoded matrix payload in
#'   column-major order (first dimension fastest).
#' @param dtype Codec name, e.g. `"f32"`, `"f16"`, `"bf16"`.
#' @param dim Length-2 integer dimensions of the decoded matrix.
#' @param x An `fmalloc_tensor` object (or, in `%*%`, a dense operand).
#' @param y The other matrix product operand.
#' @param ... Unused.
#'
#' @return `create_fmalloc_tensor()` returns an `fmalloc_tensor`.
#'   `fmalloc_tensor_materialize()` and the matrix products return
#'   fmalloc-backed double matrices. `fmalloc_tensor_codecs()` returns a
#'   character vector.
#'
#' @name fmalloc_tensor
NULL

#' @rdname fmalloc_tensor
#' @export
fmalloc_tensor_codecs <- function() {
    .Call("rfm_tensor_codec_list_impl")
}

.fmalloc_tensor_codec_info <- function(dtype) {
    if (!is.character(dtype) || length(dtype) != 1L || is.na(dtype)) {
        stop("dtype must be a single codec name string")
    }
    info <- .Call("rfm_tensor_codec_info_impl", dtype)
    if (is.null(info)) {
        stop(sprintf(
            "unknown fmalloc tensor codec '%s' (registered: %s)",
            dtype, paste(fmalloc_tensor_codecs(), collapse = ", ")
        ))
    }
    info
}

#' @rdname fmalloc_tensor
#' @export
create_fmalloc_tensor <- function(payload, dtype, dim) {
    codec <- .fmalloc_tensor_codec_info(dtype)

    if (!is_fmalloc_vector(payload) || !is.raw(payload)) {
        stop("payload must be an fmalloc raw vector")
    }
    dims <- .fmalloc_validate_dimensions(dim, "dim")
    if (length(dims) != 2L) {
        stop("fmalloc tensors currently require length-2 dims")
    }

    n_elems <- prod(as.double(dims))
    n_blocks <- ceiling(n_elems / codec$items_per_block)
    needed <- n_blocks * as.double(codec$bytes_per_block)
    if (length(payload) < needed) {
        stop(sprintf(
            "payload has %d bytes but %.0f '%s' elements need %.0f",
            length(payload), n_elems, dtype, needed
        ))
    }

    attr(payload, "rfm_dtype") <- dtype
    attr(payload, "rfm_dims") <- as.integer(dims)
    class(payload) <- "fmalloc_tensor"
    payload
}

#' @rdname fmalloc_tensor
#' @export
fmalloc_tensor_dtype <- function(x) {
    if (!inherits(x, "fmalloc_tensor")) {
        stop("x must be an fmalloc_tensor")
    }
    attr(x, "rfm_dtype")
}

#' @rdname fmalloc_tensor
#' @export
fmalloc_tensor_materialize <- function(x) {
    if (!inherits(x, "fmalloc_tensor")) {
        stop("x must be an fmalloc_tensor")
    }
    ans <- .Call(
        "rfm_tensor_materialize_impl", x,
        attr(x, "rfm_dtype"), attr(x, "rfm_dims")
    )
    .fmalloc_apply_class(ans, type = "numeric", shape = "matrix")
}

#' @rdname fmalloc_tensor
#' @export
dim.fmalloc_tensor <- function(x) {
    attr(x, "rfm_dims")
}

#' @rdname fmalloc_tensor
#' @export
print.fmalloc_tensor <- function(x, ...) {
    dims <- attr(x, "rfm_dims")
    cat(sprintf(
        "<fmalloc_tensor %s [%s], %d payload bytes>\n",
        attr(x, "rfm_dtype"), paste(dims, collapse = " x "),
        length(unclass(x))
    ))
    invisible(x)
}

.fmalloc_tensor_panel_elems <- function() {
    as.double(getOption("Rfmalloc.tensor_panel_elems", 2^23))
}

# Promotes a dense operand for `tensor %*% dense` (or the mirrored case)
# following base's vector promotion rules, returning a double matrix.
.fmalloc_tensor_dense_operand <- function(d, inner_len, dense_on_right) {
    d <- .fmalloc_strip_class(d)
    if (!(is.numeric(d) || is.logical(d))) {
        stop("dense matrix product operand must be numeric or logical")
    }
    if (is.null(dim(d))) {
        if (dense_on_right) {
            if (length(d) == inner_len) {
                dim(d) <- c(inner_len, 1L)
            } else if (inner_len == 1L) {
                dim(d) <- c(1L, length(d))
            } else {
                stop("non-conformable arguments")
            }
        } else {
            if (length(d) == inner_len) {
                dim(d) <- c(1L, inner_len)
            } else if (inner_len == 1L) {
                dim(d) <- c(length(d), 1L)
            } else {
                stop("non-conformable arguments")
            }
        }
    } else if (length(dim(d)) != 2L) {
        stop("dense matrix product operand must be a vector or matrix")
    }
    if (storage.mode(d) != "double") {
        storage.mode(d) <- "double"
    }
    d
}

.fmalloc_tensor_matmul <- function(x, y) {
    x_typed <- inherits(x, "fmalloc_tensor")
    y_typed <- inherits(y, "fmalloc_tensor")

    if (x_typed && y_typed) {
        return(.fmalloc_tensor_matmul(x, fmalloc_tensor_materialize(y)))
    }

    tensor <- if (x_typed) x else y
    tdims <- attr(tensor, "rfm_dims")
    inner_len <- if (x_typed) tdims[2L] else tdims[1L]
    dense <- .fmalloc_tensor_dense_operand(
        if (x_typed) y else x, inner_len,
        dense_on_right = x_typed
    )

    # dgemm has no NA semantics: non-finite dense operands take the
    # materialized path through the regular fmalloc matrix product.
    if (anyNA(dense) || !all(is.finite(dense))) {
        mat <- fmalloc_tensor_materialize(tensor)
        return(if (x_typed) mat %*% dense else dense %*% mat)
    }

    ans <- .Call(
        "rfm_tensor_matmul_impl", tensor,
        attr(tensor, "rfm_dtype"), tdims, dense, x_typed,
        .fmalloc_tensor_panel_elems()
    )
    .fmalloc_apply_class(ans, type = "numeric", shape = "matrix")
}

#' @rdname fmalloc_tensor
#' @method %*% fmalloc_tensor
#' @export
`%*%.fmalloc_tensor` <- function(x, y) {
    .fmalloc_tensor_matmul(x, y)
}

#' @rdname fmalloc_tensor
#' @method matrixOps fmalloc_tensor
#' @export
matrixOps.fmalloc_tensor <- function(x, y) {
    if (identical(.Generic, "%*%")) {
        return(.fmalloc_tensor_matmul(x, y))
    }
    if (identical(.Generic, "crossprod")) {
        return(crossprod.fmalloc_tensor(x, if (missing(y)) NULL else y))
    }
    if (identical(.Generic, "tcrossprod")) {
        return(tcrossprod.fmalloc_tensor(x, if (missing(y)) NULL else y))
    }
    stop("Unsupported fmalloc_tensor matrixOps generic: ", .Generic)
}

#' @rdname fmalloc_tensor
#' @method crossprod fmalloc_tensor
#' @export
crossprod.fmalloc_tensor <- function(x, y = NULL, ...) {
    x0 <- if (inherits(x, "fmalloc_tensor")) fmalloc_tensor_materialize(x) else x
    y0 <- if (inherits(y, "fmalloc_tensor")) fmalloc_tensor_materialize(y) else y
    if (is.null(y0)) crossprod(x0) else crossprod(x0, y0)
}

#' @rdname fmalloc_tensor
#' @method tcrossprod fmalloc_tensor
#' @export
tcrossprod.fmalloc_tensor <- function(x, y = NULL, ...) {
    x0 <- if (inherits(x, "fmalloc_tensor")) fmalloc_tensor_materialize(x) else x
    y0 <- if (inherits(y, "fmalloc_tensor")) fmalloc_tensor_materialize(y) else y
    if (is.null(y0)) tcrossprod(x0) else tcrossprod(x0, y0)
}
