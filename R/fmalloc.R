# Package-local default runtime used by the compatibility API.
.fmalloc_state <- new.env(parent = emptyenv())
.fmalloc_state$default_runtime <- NULL

.fmalloc_get_runtime <- function(runtime = NULL) {
    if (is.null(runtime)) {
        runtime <- .fmalloc_state$default_runtime
    }
    if (is.null(runtime)) {
        stop("fmalloc not initialized. Call init_fmalloc() first or pass a runtime from open_fmalloc().")
    }
    runtime
}

.fmalloc_validate_non_negative_integer <- function(value, arg_name) {
    if (!is.numeric(value) || length(value) != 1 || !is.finite(value) || is.na(value) ||
        value < 0 || value != floor(value)) {
        stop(sprintf("%s must be a positive integer or zero", arg_name))
    }
    as.integer(value)
}

.fmalloc_validate_dimensions <- function(dimensions, arg_name) {
    if (!is.numeric(dimensions) || length(dimensions) < 1) {
        stop(sprintf("%s must be a non-empty integer vector", arg_name))
    }

    dims <- as.numeric(dimensions)
    if (any(!is.finite(dims)) || any(is.na(dims)) || any(dims < 0) || any(dims != floor(dims))) {
        stop(sprintf("%s must be a non-negative integer vector", arg_name))
    }

    as.integer(dims)
}

.fmalloc_normalize_type <- function(type) {
    if (!is.character(type) || length(type) != 1L) {
        stop("type must be a single character string")
    }

    type <- tolower(type)
    switch(type,
        "logical" = "logical",
        "integer" = "integer",
        "numeric" = "numeric",
        "double" = "numeric",
        "real" = "numeric",
        "raw" = "raw",
        "complex" = "complex",
        "character" = "character",
        "list" = "list",
        stop("Unsupported vector type: ", type)
    )
}

.fmalloc_class_from_type <- function(type) {
    type <- .fmalloc_normalize_type(type)
    switch(type,
        "logical" = "logical",
        "integer" = "integer",
        "numeric" = "numeric",
        "raw" = "raw",
        "complex" = "complex",
        "character" = "character",
        "list" = "list"
    )
}

.fmalloc_base_type_from_object <- function(x) {
    .fmalloc_class_from_type(typeof(x))
}

.fmalloc_shape_class <- function(x) {
    dim_values <- dim(x)
    if (is.null(dim_values)) {
        return("vector")
    }
    if (length(dim_values) == 2L) {
        "matrix"
    } else {
        "array"
    }
}

.fmalloc_reduction_result_threshold <- function() {
    threshold <- getOption("Rfmalloc.reduce_result_length", 1e6)
    if (is.null(threshold)) {
        return(1e6)
    }
    if (!is.numeric(threshold) || length(threshold) != 1L || !is.finite(threshold) || threshold < 0L) {
        warning("Invalid option Rfmalloc.reduce_result_length; using default 1000000")
        return(1e6)
    }
    as.integer(threshold)
}

.fmalloc_set_class <- function(x, class_value) {
    .Call("fmalloc_set_class_in_place_impl", x, as.character(class_value))
}

.fmalloc_apply_class <- function(x, type = NULL, shape = NULL) {
    if (!is.null(type)) {
        base_class <- .fmalloc_class_from_type(type)
    } else {
        base_class <- .fmalloc_base_type_from_object(x)
    }

    if (is.null(shape)) {
        shape <- .fmalloc_shape_class(x)
    }

    shape_class <- switch(shape,
        "matrix" = c("fmalloc_matrix", "matrix", "fmalloc", base_class),
        "array" = c("fmalloc_array", "array", "fmalloc", base_class),
        c("fmalloc_vector", "fmalloc", base_class)
    )
    .fmalloc_set_class(x, shape_class)
}

.fmalloc_strip_class <- function(x) {
    if (!is.object(x) || !inherits(x, "fmalloc")) {
        return(x)
    }
    class(x) <- NULL
    x
}

.fmalloc_runtime_for_vector <- function(x) {
    .Call("fmalloc_runtime_of_vector_impl", x)
}

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
#'   only accept `NULL` or fmalloc-backed vectors from the same runtime as
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

    type <- .fmalloc_normalize_type(type)
    template <- switch(
        type,
        "logical" = logical(0),
        "integer" = integer(0),
        "numeric" = numeric(0),
        "raw" = raw(0),
        "complex" = complex(0),
        "character" = character(0),
        "list" = vector("list", 0),
        stop("Unsupported vector type: ", type)
    )

    runtime <- .fmalloc_get_runtime(runtime)

    ans <- .Call("create_fmalloc_vector_impl", runtime, template, as.integer(length))
    .fmalloc_apply_class(ans, type = type, shape = "vector")
}

#' Create Matrix Using fmalloc
#'
#' Creates an fmalloc-backed ALTREP matrix in a single step by allocating vector
#' storage and installing matrix dimensions (and optional dimnames).
#'
#' @param type Character string specifying the vector type. Supported values are
#'   the same as for [create_fmalloc_vector()].
#' @param nrow Integer number of rows.
#' @param ncol Integer number of columns.
#' @param dimnames Optional `dimnames` list for the matrix.
#' @param runtime Optional runtime handle returned by [open_fmalloc()]. If not
#'   supplied, the default runtime established by [init_fmalloc()] is used.
#'
#' @return An fmalloc-backed ALTREP matrix object.
#'
#' @examples
#' \dontrun{
#' rt <- open_fmalloc(tempfile(fileext = ".bin"))
#' m <- create_fmalloc_matrix("integer", nrow = 2, ncol = 3, runtime = rt)
#' cleanup_fmalloc(rt)
#' }
#'
#' @export
create_fmalloc_matrix <- function(type = "integer", nrow, ncol,
                                 dimnames = NULL, runtime = NULL) {
    if (missing(nrow) || missing(ncol)) {
        stop("nrow and ncol are required")
    }

    nrow <- .fmalloc_validate_non_negative_integer(nrow, "nrow")
    ncol <- .fmalloc_validate_non_negative_integer(ncol, "ncol")

    total <- as.double(nrow) * as.double(ncol)
    if (!is.finite(total) || total > .Machine$integer.max) {
        stop("length is too large for the current fmalloc vector interface")
    }

    ans <- create_fmalloc_vector(type = type, length = as.integer(total), runtime = runtime)
    dim(ans) <- c(nrow, ncol)
    if (!is.null(dimnames)) {
        dimnames(ans) <- dimnames
    }
    .fmalloc_apply_class(ans, shape = "matrix")
}


#' Create Array Using fmalloc
#'
#' Creates an fmalloc-backed ALTREP array in a single step by allocating vector
#' storage and installing array dimensions (and optional dimnames).
#'
#' @param type Character string specifying the vector type. Supported values are
#'   the same as for [create_fmalloc_vector()].
#' @param dim Integer dimension vector.
#' @param dimnames Optional `dimnames` for the array.
#' @param runtime Optional runtime handle returned by [open_fmalloc()]. If not
#'   supplied, the default runtime established by [init_fmalloc()] is used.
#'
#' @return An fmalloc-backed ALTREP array.
#'
#' @examples
#' \dontrun{
#' rt <- open_fmalloc(tempfile(fileext = ".bin"))
#' a <- create_fmalloc_array("numeric", dim = c(2L, 3L), runtime = rt)
#' cleanup_fmalloc(rt)
#' }
#'
#' @export
create_fmalloc_array <- function(type = "integer", dim, dimnames = NULL, runtime = NULL) {
    dims <- .fmalloc_validate_dimensions(dim, "dim")
    total <- as.double(prod(dims))
    if (!is.finite(total) || total > .Machine$integer.max) {
        stop("length is too large for the current fmalloc vector interface")
    }

    ans <- create_fmalloc_vector(type = type, length = as.integer(total), runtime = runtime)
    dim(ans) <- dims
    if (!is.null(dimnames)) {
        dimnames(ans) <- dimnames
    }
    .fmalloc_apply_class(ans, shape = "array")
}


#' Construct data.frame from fmalloc columns
#'
#' Thin constructor wrapper around [data.frame()] that keeps fmalloc vectors as
#' column payloads.
#'
#' @param ... Columns to include in the frame.
#' @param row.names Optional row names for the frame.
#' @param check.names Whether to enforce syntactic column names.
#' @param stringsAsFactors Deprecated: retained for compatibility.
#'
#' @return A `data.frame` with the provided columns.
#'
#' @examples
#' \dontrun{
#' rt <- open_fmalloc(tempfile(fileext = ".bin"))
#' x <- create_fmalloc_vector("integer", 3, runtime = rt)
#' y <- create_fmalloc_vector("character", 3, runtime = rt)
#' x[] <- 1:3
#' y[] <- c("a", "b", "c")
#' df <- create_fmalloc_data_frame(x = x, y = y)
#' cleanup_fmalloc(rt)
#' }
#'
#' @export
create_fmalloc_data_frame <- function(..., row.names = NULL,
                                     check.names = TRUE,
                                     stringsAsFactors = FALSE) {
    data.frame(
        ..., row.names = row.names, check.names = check.names,
        stringsAsFactors = stringsAsFactors
    )
}

.fmalloc_set_dim_attrs <- function(x, dim, dimnames) {
    .Call("fmalloc_set_dims_in_place_impl", x, dim, dimnames)
}

#' Convert a vector to fmalloc matrix metadata
#'
#' Returns an existing vector re-typed as a matrix by installing matrix
#' dimensions (and optional dimnames) as metadata.
#'
#' @param x A vector.
#' @param nrow Optional target row count.
#' @param ncol Optional target column count.
#' @param dimnames Optional `dimnames` for the resulting matrix.
#' @param copy If TRUE (default), allocate a new fmalloc-backed matrix object.
#'   If FALSE, install metadata in place on the same fmalloc ALTREP payload
#'   without allocation (this also updates any aliases of `x`).
#'
#' @return A matrix object, backed by the same payload when `copy = FALSE`.
#'
#' @export
as_fmalloc_matrix <- function(x, nrow = NULL, ncol = NULL, dimnames = NULL, copy = TRUE) {
    vec_len <- as.integer(length(x))

    if (missing(nrow) && missing(ncol)) {
        nrow <- vec_len
        ncol <- 1L
    }

    if (!is.null(nrow)) {
        nrow <- .fmalloc_validate_non_negative_integer(nrow, "nrow")
    }
    if (!is.null(ncol)) {
        ncol <- .fmalloc_validate_non_negative_integer(ncol, "ncol")
    }

    if (is.null(nrow) && is.null(ncol)) {
        nrow <- vec_len
        ncol <- 1L
    } else if (is.null(nrow)) {
        if (ncol == 0L) {
            if (vec_len != 0L) {
                stop("length of x is not compatible with ncol")
            }
            nrow <- 0L
        } else {
            if (vec_len %% ncol != 0L) {
                stop("length of x is not a multiple of ncol")
            }
            nrow <- as.integer(vec_len / ncol)
        }
    } else if (is.null(ncol)) {
        if (nrow == 0L) {
            if (vec_len != 0L) {
                stop("length of x is not compatible with nrow")
            }
            ncol <- 0L
        } else {
            if (vec_len %% nrow != 0L) {
                stop("length of x is not a multiple of nrow")
            }
            ncol <- as.integer(vec_len / nrow)
        }
    } else {
        if (as.double(nrow) * as.double(ncol) != as.double(vec_len)) {
            stop("nrow and ncol do not match length of x")
        }
    }

    dims <- c(nrow, ncol)
    if (copy) {
        dim(x) <- dims
        if (!is.null(dimnames)) {
            dimnames(x) <- dimnames
        }
        if (inherits(x, "fmalloc")) {
            x <- .fmalloc_apply_class(x, shape = "matrix")
        }
        return(x)
    }

    .fmalloc_set_dim_attrs(x, dims, dimnames)
    if (inherits(x, "fmalloc")) {
        x <- .fmalloc_apply_class(x, shape = "matrix")
    }
    x
}

#' Convert a vector to fmalloc array metadata
#'
#' Returns an existing vector re-typed as an array by installing array
#' dimensions (and optional dimnames) as metadata.
#'
#' @param x A vector.
#' @param dim Target dimension vector.
#' @param dimnames Optional `dimnames` for the resulting array.
#' @param copy If TRUE (default), allocate a new fmalloc-backed array object.
#'   If FALSE, install metadata in place on the same fmalloc ALTREP payload
#'   without allocation (this also updates any aliases of `x`).
#'
#' @return An array object, backed by the same payload when `copy = FALSE`.
#'
#' @export
as_fmalloc_array <- function(x, dim = NULL, dimnames = NULL, copy = TRUE) {
    vec_len <- as.integer(length(x))

    if (is.null(dim)) {
        dim <- as.integer(vec_len)
    } else {
        dim <- .fmalloc_validate_dimensions(dim, "dim")
        total <- as.double(prod(dim))
        if (total != as.double(vec_len)) {
            stop("dim does not match the length of x")
        }
    }

    if (copy) {
        dim(x) <- dim
        if (!is.null(dimnames)) {
            dimnames(x) <- dimnames
        }
        if (inherits(x, "fmalloc")) {
            x <- .fmalloc_apply_class(x, shape = "array")
        }
        return(x)
    }

    .fmalloc_set_dim_attrs(x, dim, dimnames)
    if (inherits(x, "fmalloc")) {
        x <- .fmalloc_apply_class(x, shape = "array")
    }
    x
}

#' Convert to data.frame for fmalloc vectors
#'
#' Thin convenience wrapper around [data.frame()].
#'
#' @param ... Columns or objects to include in the frame.
#' @param row.names Optional row names for the frame.
#' @param check.names Whether to enforce syntactic column names.
#' @param stringsAsFactors Deprecated: retained for compatibility.
#'
#' @return A `data.frame` containing the supplied columns.
#'
#' @export
as_fmalloc_data_frame <- function(..., row.names = NULL,
                                  check.names = TRUE,
                                  stringsAsFactors = FALSE) {
    data.frame(
        ..., row.names = row.names, check.names = check.names,
        stringsAsFactors = stringsAsFactors
    )
}

#' List Persistent fmalloc Allocations
#'
#' Returns the in-file allocation catalog for a persistent fmalloc runtime. The
#' catalog is stored in the backing file and records physical allocation metadata
#' used to validate serialized persistent references.
#'
#' For successful recovery, look at the `state` column:
#'
#' - `"committed"`: valid serialized payload exists for that record;
#' - `"tombstone"`: the payload has been destroyed and is non-recoverable unless
#'   the runtime remains open and referenced directly by an existing SEXP;
#' - other transient states are internal and are generally not expected.
#'
#' `recoverable` indicates whether the record can be reopened via serialized
#' reference metadata. `payload_offset == 0` or `payload_nbytes == 0` generally
#' indicates a non-payload entry.
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
    runtime <- .fmalloc_get_runtime(runtime)

    .Call("list_fmalloc_allocations_impl", runtime)
}

.summarize_catalog_records <- function(catalog_df) {
    if (nrow(catalog_df) == 0L) {
        return(list(
            record_count = 0L,
            committed_records = 0L,
            tombstoned_records = 0L,
            unknown_records = 0L,
            recoverable_records = 0L,
            nonrecoverable_active_records = 0L,
            active_payload_bytes = 0,
            tombstoned_payload_bytes = 0,
            committed_payload_gaps_bytes = 0,
            potentially_reclaimable_payload_bytes = 0,
            compaction_implemented = FALSE,
            compaction_note = "Catalog compaction is currently not implemented; catalog entries are append-only to preserve serialized (record offset, generation) references."
        ))
    }

    state <- as.character(catalog_df$state)
    payload <- as.double(catalog_df$payload_nbytes)
    recoverable <- as.logical(catalog_df$recoverable)
    payload_offset <- as.double(catalog_df$payload_offset)

    committed <- state == "committed"
    tombstoned <- state == "tombstone"
    unknown <- !committed & !tombstoned

    active_intervals <- catalog_df[committed & payload_offset > 0 & payload > 0,
                                  c("payload_offset", "payload_nbytes"), drop = FALSE]
    payload_gaps <- 0
    if (nrow(active_intervals) > 1L) {
        ord <- order(active_intervals$payload_offset)
        starts <- as.double(active_intervals$payload_offset[ord])
        ends <- starts + as.double(active_intervals$payload_nbytes[ord])
        gap <- starts[-1L] - ends[-nrow(active_intervals)]
        payload_gaps <- sum(gap[gap > 0], na.rm = TRUE)
    }

    tombstone_payload <- sum(payload[tombstoned], na.rm = TRUE)
    active_payload <- sum(payload[committed], na.rm = TRUE)

    list(
        record_count = as.integer(nrow(catalog_df)),
        committed_records = as.integer(sum(committed, na.rm = TRUE)),
        tombstoned_records = as.integer(sum(tombstoned, na.rm = TRUE)),
        unknown_records = as.integer(sum(unknown, na.rm = TRUE)),
        recoverable_records = as.integer(sum(recoverable, na.rm = TRUE)),
        nonrecoverable_active_records = as.integer(sum(committed & !recoverable, na.rm = TRUE)),
        active_payload_bytes = as.double(active_payload),
        tombstoned_payload_bytes = as.double(tombstone_payload),
        committed_payload_gaps_bytes = as.double(payload_gaps),
        potentially_reclaimable_payload_bytes = as.double(tombstone_payload + payload_gaps),
        compaction_implemented = FALSE,
        compaction_note = "Catalog compaction is currently not implemented; catalog entries are append-only so reference offsets and generation values remain stable across a runtime's lifetime."
    )
}

#' Diagnose fmalloc runtime state
#'
#' Returns diagnostic metadata for an open runtime handle, including lightweight
#' runtime attributes, the current allocation catalog, and a catalog-level summary
#' useful for estimating reclaimable/fragmented payload regions.
#'
#' @param runtime Optional runtime handle returned by [open_fmalloc()]. If not
#'   supplied, the current default runtime is used.
#'
#' @return A named list with three components:
#'
#' - `runtime`: runtime metadata such as file path, UUID, mode, catalog
#'   counters, live vectors, and reference state;
#' - `catalog`: the full allocation catalog returned by
#'   [list_fmalloc_allocations()];
#' - `summary`: a compact set of computed diagnostics and an explicit compaction
#'   status note.
#'
#' @examples
#' \dontrun{
#' rt <- open_fmalloc(tempfile(fileext = ".bin"), mode = "persistent")
#' x <- create_fmalloc_vector("integer", 4, runtime = rt)
#' y <- create_fmalloc_vector("logical", 2, runtime = rt)
#' diagnose_fmalloc_runtime(rt)
#' cleanup_fmalloc(rt)
#' }
#'
#' @export
#'
#' @seealso [list_fmalloc_allocations()]
#'
diagnose_fmalloc_runtime <- function(runtime = NULL) {
    runtime <- .fmalloc_get_runtime(runtime)
    catalog <- list_fmalloc_allocations(runtime)
    info <- .Call("fmalloc_runtime_info_impl", runtime)
    summary <- .summarize_catalog_records(catalog)

    list(
        runtime = info,
        catalog = catalog,
        summary = summary
    )
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

#' Explicitly destroy a fmalloc vector
#'
#' Releases runtime bookkeeping for a single fmalloc ALTREP vector immediately. In
#' scratch mode, payload memory is immediately reclaimed. In persistent mode, the
#' vector payload is retained by default so existing on-disk state remains
#' durable; optional `unsafe = TRUE` reclaims payload memory and marks metadata
#' as non-recoverable.
#'
#' Explicit destroy fails when a vector is still referenced by another fmalloc list
#' vector as a child.
#'
#' @param x Fmalloc ALTREP vector to destroy.
#' @param unsafe Whether to physically free persistent payload bytes. Unsafe
#'   destroy is intended for short-lived scratch-like cleanup and will mark the
#'   catalog entry as non-recoverable.
#'
#' @return Logical value indicating whether a live vector was destroyed.
#'
#' @examples
#' \dontrun{
#' rt <- open_fmalloc(tempfile(fileext = ".bin"), mode = "persistent")
#' v <- create_fmalloc_vector("integer", 10, runtime = rt)
#' destroy_fmalloc_vector(v)
#' }
#'
#' @export
destroy_fmalloc_vector <- function(x, unsafe = FALSE) {
    if (!is.logical(unsafe) || length(unsafe) != 1 || is.na(unsafe)) {
        stop("unsafe must be a single logical value")
    }

    .Call("destroy_fmalloc_vector_impl", x, as.logical(unsafe))
}
