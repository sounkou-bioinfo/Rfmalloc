#' Initialize fmalloc
#'
#' Initializes the fmalloc memory allocator with a backing file.
#' This must be called before using fmalloc-based allocation functions.
#' fmalloc supports malloc, free, and realloc patterns for persistent
#' memory allocation backed by memory-mapped files.
#'
#' @param filepath Character string specifying the file path for fmalloc data
#' @param size_gb Numeric value specifying the size of the backing file in GB (optional)
#'                If not specified, uses default size or existing file size
#'
#' @return Logical indicating whether the file was newly initialized
#'
#' @examples
#' \dontrun{
#' # Initialize fmalloc with default size
#' init_result <- init_fmalloc("fmalloc_data.bin")
#'
#' # Initialize fmalloc with specific size (50 GB)
#' init_result <- init_fmalloc("large_data.bin", size_gb = 50)
#'
#' # Create vectors using fmalloc (supports realloc patterns)
#' v <- create_fmalloc_vector("integer", 1000)
#'
#' # Clean up
#' cleanup_fmalloc()
#' }
#'
#' @export
init_fmalloc <- function(filepath, size_gb = NULL) {
    if (!is.character(filepath) || length(filepath) != 1) {
        stop("filepath must be a single character string")
    }

    if (!is.null(size_gb)) {
        if (!is.numeric(size_gb) || length(size_gb) != 1 || size_gb <= 0) {
            stop("size_gb must be a positive numeric value")
        }
    }

    .Call("init_fmalloc_impl", filepath, size_gb)
}

#' Create Vector Using fmalloc
#'
#' Creates a vector using the fmalloc allocator with realloc support.
#' The fmalloc system must be initialized first using \code{init_fmalloc()}.
#' The underlying allocator supports malloc, free, and realloc patterns
#' for efficient memory management backed by memory-mapped files.
#'
#' @param type Character string specifying the vector type ("integer", "numeric", "logical")
#' @param length Integer specifying the length of the vector to create
#'
#' @return A vector of the specified type and length, allocated using fmalloc
#'
#' @examples
#' \dontrun{
#' # Initialize fmalloc first
#' init_fmalloc("fmalloc_data.bin")
#'
#' # Create vectors with realloc support
#' v1 <- create_fmalloc_vector("integer", 1000)
#' v2 <- create_fmalloc_vector("numeric", 500)
#'
#' # Use the vectors normally - R will handle realloc internally
#' v1[1:10] <- 1:10
#' v2[1:5] <- runif(5)
#'
#' # Clean up
#' cleanup_fmalloc()
#' }
#'
#' @export
create_fmalloc_vector <- function(type = "integer", length) {
    if (!is.character(type) || length(type) != 1) {
        stop("type must be a single character string")
    }
    if (!is.numeric(length) || length(length) != 1 || length < 1) {
        stop("length must be a positive integer")
    }

    template <- switch(
        type,
        "integer" = integer(0),
        "numeric" = numeric(0),
        "logical" = logical(0),
        stop("Unsupported vector type: ", type)
    )

    .Call("create_fmalloc_vector_impl", template, as.integer(length))
}

#' Clean Up fmalloc
#'
#' Cleans up the fmalloc allocator and releases resources.
#' Call this when you're done using fmalloc-based allocation.
#'
#' @return NULL (invisibly)
#'
#' @examples
#' \dontrun{
#' init_fmalloc("data.bin")
#' v <- create_fmalloc_vector("integer", 100)
#' # ... use vector ...
#' cleanup_fmalloc()
#' }
#'
#' @export
cleanup_fmalloc <- function() {
    invisible(.Call("cleanup_fmalloc_impl"))
}
