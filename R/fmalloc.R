#' Create Memory-Mapped Vector
#'
#' Creates a memory-mapped integer vector backed by a file using ALTREP.
#' The vector data is stored in the specified file and can persist across
#' R sessions.
#'
#' @param filepath Character string specifying the file path for the memory-mapped data
#' @param length Integer specifying the length of the vector to create
#'
#' @return An ALTREP integer vector backed by memory-mapped file
#'
#' @examples
#' \dontrun{
#' # Create a memory-mapped vector
#' v <- create_mmap_vector("data.bin", 1000)
#' v[1:10] <- 1:10
#' print(v[1:10])
#' }
#'
#' @export
create_mmap_vector <- function(filepath, length) {
    if (!is.character(filepath) || length(filepath) != 1) {
        stop("filepath must be a single character string")
    }
    if (!is.numeric(length) || length(length) != 1 || length < 1) {
        stop("length must be a positive integer")
    }

    .Call("create_mmap_vector_impl", filepath, as.integer(length))
}

#' Initialize fmalloc
#'
#' Initializes the fmalloc memory allocator with a backing file.
#' This must be called before using fmalloc-based allocation functions.
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
#' # Create vectors using fmalloc
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
#' Creates a vector using the fmalloc allocator. The fmalloc system must
#' be initialized first using \code{init_fmalloc()}.
#'
#' @param type Character string specifying the vector type ("integer", "numeric", "logical", "character")
#' @param length Integer specifying the length of the vector to create
#'
#' @return A vector of the specified type and length, allocated using fmalloc
#'
#' @examples
#' \dontrun{
#' # Initialize fmalloc first
#' init_fmalloc("fmalloc_data.bin")
#'
#' # Create vectors
#' v1 <- create_fmalloc_vector("integer", 1000)
#' v2 <- create_fmalloc_vector("numeric", 500)
#'
#' # Use the vectors normally
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

    template <- switch(type,
        "integer" = integer(0),
        "numeric" = numeric(0),
        "logical" = logical(0),
        "character" = character(0),
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
