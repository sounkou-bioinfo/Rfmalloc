# Package-local default runtime used by the compatibility API.
.fmalloc_state <- new.env(parent = emptyenv())
.fmalloc_state$default_runtime <- NULL

#' Open an fmalloc Runtime
#'
#' Opens a file-backed fmalloc runtime and returns an external-pointer handle.
#' Multiple runtime handles may be open in one R process. Runtime mode controls
#' whether vector payloads are durable persistent allocations or scratch
#' allocations that can be returned to fmalloc when their ALTREP handles are
#' garbage-collected.
#'
#' @param filepath Character string specifying the file path for fmalloc data.
#' @param size_gb Numeric value specifying the size of the backing file in GB
#'   (optional). If not specified, uses the package default size for new files
#'   or the existing file size.
#' @param mode Runtime mode. `"persistent"` keeps committed vector payloads in
#'   the backing file and serializes fixed-width atomic vectors by reference.
#'   `"scratch"` uses the backing file as a large temporary allocation arena and
#'   serializes vectors by value.
#'
#' @return An external pointer of class `fmalloc_runtime`.
#'
#' @export
open_fmalloc <- function(filepath, size_gb = NULL, mode = c("persistent", "scratch")) {
    if (!is.character(filepath) || length(filepath) != 1) {
        stop("filepath must be a single character string")
    }

    if (!is.null(size_gb)) {
        if (!is.numeric(size_gb) || length(size_gb) != 1 || size_gb <= 0) {
            stop("size_gb must be a positive numeric value")
        }
    }
    mode <- match.arg(mode)

    .Call("open_fmalloc_impl", filepath, size_gb, mode)
}

#' Initialize fmalloc
#'
#' Compatibility wrapper that opens an fmalloc runtime and installs it as the
#' package default runtime used by [create_fmalloc_vector()] when no explicit
#' runtime is supplied.
#'
#' For new code, prefer [open_fmalloc()] and pass the returned runtime handle to
#' [create_fmalloc_vector()].
#'
#' @inheritParams open_fmalloc
#'
#' @return Logical indicating whether the file was newly initialized.
#'
#' @examples
#' \dontrun{
#' alloc_file <- tempfile(fileext = ".bin")
#' init_fmalloc(alloc_file)
#' v <- create_fmalloc_vector("integer", 1000)
#' cleanup_fmalloc()
#' unlink(alloc_file)
#' }
#'
#' @export
init_fmalloc <- function(filepath, size_gb = NULL, mode = c("persistent", "scratch")) {
    mode <- match.arg(mode)
    rt <- open_fmalloc(filepath, size_gb, mode = mode)

    old_rt <- .fmalloc_state$default_runtime
    if (!is.null(old_rt)) {
        warning("fmalloc already initialized; replacing default runtime")
        cleanup_fmalloc(old_rt)
    }

    .fmalloc_state$default_runtime <- rt
    isTRUE(attr(rt, "initialized"))
}

#' Create Vector Using fmalloc
#'
#' Creates an ALTREP vector using a file-backed fmalloc runtime. The returned
#' object is ALTREP from creation time. Fixed-width atomic payload bytes are
#' allocated directly with fmalloc, and ALTREP duplication and vector subsetting
#' keep copy-on-write copies fmalloc-backed without using R's non-API
#' `Rf_allocVector3()` path.
#'
#' @param type Character string specifying the vector type. Supported values are
#'   `"logical"`, `"integer"`, `"numeric"`/`"double"`, `"raw"`,
#'   `"complex"`, `"character"`, and `"list"`. Fixed-width atomic types expose
#'   a direct writable fmalloc `DATAPTR`; character vectors store string bytes in
#'   fmalloc and materialize R `CHARSXP` values on demand; list vectors use
#'   ALTREP element access with an R-visible reference sidecar for GC safety and
#'   only accept `NULL` or Rfmalloc-backed vectors from the same runtime as
#'   elements. Persistent list containers are serialized by nested reference
#'   states when all elements are recoverable from the same runtime.
#' @param length Integer specifying the non-negative length of the vector to
#'   create.
#' @param runtime Optional runtime handle returned by [open_fmalloc()]. If not
#'   supplied, the default runtime established by [init_fmalloc()] is used.
#'
#' @return A vector of the specified type and length, allocated using fmalloc.
#'
#' @examples
#' \dontrun{
#' rt <- open_fmalloc(tempfile(fileext = ".bin"))
#' v <- create_fmalloc_vector("integer", 1000, runtime = rt)
#' cleanup_fmalloc(rt)
#' }
#'
#' @export
create_fmalloc_vector <- function(type = "integer", length, runtime = NULL) {
    if (!is.character(type) || length(type) != 1) {
        stop("type must be a single character string")
    }
    if (
        !is.numeric(length) || length(length) != 1 ||
            !is.finite(length) || is.na(length) ||
            length < 0 || length != floor(length)
    ) {
        stop("length must be a positive integer or zero")
    }
    if (length > .Machine$integer.max) {
        stop("length is too large for the current fmalloc vector interface")
    }

    template <- switch(
        type,
        "logical" = logical(0),
        "integer" = integer(0),
        "numeric" = numeric(0),
        "double" = numeric(0),
        "real" = numeric(0),
        "raw" = raw(0),
        "complex" = complex(0),
        "character" = character(0),
        "list" = vector("list", 0),
        stop("Unsupported vector type: ", type)
    )

    if (is.null(runtime)) {
        runtime <- .fmalloc_state$default_runtime
    }
    if (is.null(runtime)) {
        stop("fmalloc not initialized. Call init_fmalloc() first or pass a runtime from open_fmalloc().")
    }

    .Call("create_fmalloc_vector_impl", runtime, template, as.integer(length))
}

#' List Persistent fmalloc Allocations
#'
#' Returns the in-file allocation catalog for a persistent fmalloc runtime. The
#' catalog is stored in the backing file and records physical allocation metadata
#' used to validate serialized persistent references.
#'
#' @param runtime Optional runtime handle returned by [open_fmalloc()]. If not
#'   supplied, the default runtime established by [init_fmalloc()] is used.
#'
#' @return A data frame with one row per catalog record and columns describing
#'   the catalog record offset, generation, state, vector type, length, payload
#'   offset, payload byte size, flags, and whether the record is recoverable by
#'   reference serialization.
#'
#' @examples
#' \dontrun{
#' rt <- open_fmalloc(tempfile(fileext = ".bin"))
#' v <- create_fmalloc_vector("integer", 10, runtime = rt)
#' list_fmalloc_allocations(rt)
#' cleanup_fmalloc(rt)
#' }
#'
#' @export
list_fmalloc_allocations <- function(runtime = NULL) {
    if (is.null(runtime)) {
        runtime <- .fmalloc_state$default_runtime
    }
    if (is.null(runtime)) {
        stop("fmalloc not initialized. Call init_fmalloc() first or pass a runtime from open_fmalloc().")
    }

    .Call("list_fmalloc_allocations_impl", runtime)
}

#' Clean Up fmalloc
#'
#' Requests cleanup of an fmalloc runtime. If vectors allocated from the runtime
#' are still reachable, the native mapping is kept alive until those vectors are
#' garbage-collected.
#'
#' @param runtime Optional runtime handle returned by [open_fmalloc()]. If not
#'   supplied, the current default runtime is cleaned up.
#'
#' @return NULL (invisibly)
#'
#' @examples
#' \dontrun{
#' init_fmalloc("data.bin")
#' v <- create_fmalloc_vector("integer", 100)
#' rm(v)
#' gc()
#' cleanup_fmalloc()
#' }
#'
#' @export
cleanup_fmalloc <- function(runtime = NULL) {
    if (is.null(runtime)) {
        runtime <- .fmalloc_state$default_runtime
        .fmalloc_state$default_runtime <- NULL
    } else if (identical(runtime, .fmalloc_state$default_runtime)) {
        .fmalloc_state$default_runtime <- NULL
    }

    invisible(.Call("cleanup_fmalloc_impl", runtime))
}
