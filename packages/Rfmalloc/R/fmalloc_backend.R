#' Pluggable matrix-multiply backend
#'
#' Rfmalloc's matrix-product kernels (`%*%`, `crossprod()`, `tcrossprod()`,
#' the out-of-core and typed-tensor products) dispatch their `dgemm` calls
#' through a selectable backend. By default this is R's BLAS; downstream
#' packages can register an alternative (for example a GPU cuBLAS kernel or an
#' out-of-core-aware GEMM) through the `Rfmalloc_register_matmul_backend`
#' C-callable and select it here. Selection is Rfmalloc-scoped: base R's `%*%`
#' is unaffected.
#'
#' A registered backend may decline a given call (returning non-zero), in which
#' case Rfmalloc falls back to R's BLAS for that product.
#'
#' @param name Backend name to select. `"blas"` (or `NULL`/`""`) selects the
#'   default BLAS path.
#'
#' @return `fmalloc_matmul_backend()` returns the active backend name;
#'   `fmalloc_matmul_backends()` returns the registered backend names (BLAS is
#'   always available and not listed).
#'
#' @examples
#' fmalloc_matmul_backend()      # "blas" by default
#' fmalloc_matmul_backends()     # names registered by other packages
#'
#' @name fmalloc_backend
NULL

#' @rdname fmalloc_backend
#' @export
fmalloc_matmul_backend <- function(name = NULL) {
    if (is.null(name)) {
        return(.Call("rfm_matmul_backend_impl"))
    }
    if (!is.character(name) || length(name) != 1L || is.na(name)) {
        stop("name must be a single string, or NULL to query")
    }
    invisible(.Call("rfm_set_matmul_backend_impl", name))
}

#' @rdname fmalloc_backend
#' @export
fmalloc_matmul_backends <- function() {
    .Call("rfm_matmul_backends_impl")
}
