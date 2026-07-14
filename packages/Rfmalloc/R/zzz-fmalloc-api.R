.fmalloc_type_label_from_sexptype <- function(code) {
    switch(as.character(as.integer(code)),
        "10" = "logical",
        "13" = "integer",
        "14" = "numeric",
        "15" = "complex",
        "16" = "character",
        "19" = "list",
        "24" = "raw",
        paste0("sexptype_", as.integer(code))
    )
}

.fmalloc_validate_flag <- function(value, arg_name) {
    if (!is.logical(value) || length(value) != 1L || is.na(value)) {
        stop(sprintf("%s must be a single non-missing logical value", arg_name))
    }
    value
}

#' Public Rfmalloc API helpers
#'
#' Introspection and lifecycle helpers for fmalloc runtime handles and fmalloc
#' ALTREP vectors. These functions are thin R wrappers around the package's
#' native registered routines and mirror the installed C-callable API.
#'
#' @param x An object to test or inspect. For vector helpers, `x` must be an
#'   active fmalloc ALTREP vector unless the function name starts with `is_`.
#' @param runtime Optional runtime handle returned by [open_fmalloc()]. If not
#'   supplied, the current default runtime from [init_fmalloc()] is used.
#' @param label Logical scalar. If TRUE, return an R type label such as
#'   `"integer"`; if FALSE, return the underlying R `SEXPTYPE` integer code.
#'
#' @return Depends on the helper: logical predicates, runtime external pointers,
#'   metadata lists, or payload external pointers.
#'
#' @name fmalloc_api
NULL

#' @rdname fmalloc_api
#' @export
fmalloc_default_runtime <- function() {
    runtime <- .fmalloc_state$default_runtime
    if (is_fmalloc_runtime(runtime)) {
        return(runtime)
    }

    runtime <- .Call("fmalloc_default_runtime_impl")
    if (is_fmalloc_runtime(runtime)) {
        runtime
    } else {
        NULL
    }
}

#' @rdname fmalloc_api
#' @export
is_fmalloc_runtime <- function(x) {
    isTRUE(.Call("fmalloc_is_runtime_impl", x))
}

#' @rdname fmalloc_api
#' @export
is_fmalloc_vector <- function(x) {
    isTRUE(.Call("fmalloc_is_fmalloc_vector_impl", x))
}

#' @rdname fmalloc_api
#' @export
fmalloc_runtime <- function(x) {
    .Call("fmalloc_runtime_of_vector_impl", x)
}

#' @rdname fmalloc_api
#' @export
fmalloc_runtime_info <- function(runtime = NULL) {
    runtime <- .fmalloc_get_runtime(runtime)
    .Call("fmalloc_runtime_info_impl", runtime)
}

#' @rdname fmalloc_api
#' @export
fmalloc_vector_info <- function(x) {
    info <- .Call("fmalloc_vector_info_impl", x)
    class(info) <- c("fmalloc_vector_info", class(info))
    info
}

#' @rdname fmalloc_api
#' @export
fmalloc_vector_type <- function(x, label = TRUE) {
    label <- .fmalloc_validate_flag(label, "label")
    code <- .Call("fmalloc_vector_type_impl", x)
    if (label) {
        .fmalloc_type_label_from_sexptype(code)
    } else {
        as.integer(code)
    }
}

#' @rdname fmalloc_api
#' @export
fmalloc_vector_length <- function(x) {
    .Call("fmalloc_vector_length_impl", x)
}

#' @rdname fmalloc_api
#' @export
fmalloc_vector_payload_ptr <- function(x) {
    .Call("fmalloc_vector_payload_ptr_impl", x)
}
