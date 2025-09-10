#' Initialize fmalloc
#'
#' Initializes the fmalloc memory allocator with a backing file.
#' This must be called before using fmalloc-based allocation functions.
#' fmalloc supports malloc, free, and realloc patterns for persistent
#' memory allocation backed by memory-mapped files.
#'
#' @param filepath Character string specifying the file path for fmalloc data
#'
#' @return Logical indicating whether the file was newly initialized
#'
#' @examples
#' \dontrun{
#' # Initialize fmalloc
#' init_result <- init_fmalloc("fmalloc_data.bin")
#'
#' # Create vectors using fmalloc (supports realloc patterns)
#' v <- create_fmalloc_vector("integer", 1000)
#'
#' # Clean up
#' cleanup_fmalloc()
#' }
#'
#' @export
init_fmalloc <- function(filepath) {
    if (!is.character(filepath) || length(filepath) != 1) {
        stop("filepath must be a single character string")
    }

    .Call("init_fmalloc_impl", filepath)
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
