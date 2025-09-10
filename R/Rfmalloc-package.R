#' fmalloc: Memory-Mapped File Allocation for R
#'
#' The fmalloc package provides memory-mapped file allocation capabilities for R.
#' It offers two main approaches:
#'
#' 1. **ALTREP-based memory-mapped vectors**: Using R's Alternative Representation
#'    framework to create vectors backed by memory-mapped files.
#' 2. **fmalloc allocator**: Using the fmalloc library for sophisticated
#'    persistent memory allocation.
#'
#' @section Main Functions:
#' \describe{
#'   \item{\code{\link{create_mmap_vector}}}{Create ALTREP memory-mapped vectors}
#'   \item{\code{\link{init_fmalloc}}}{Initialize the fmalloc allocator}
#'   \item{\code{\link{create_fmalloc_vector}}}{Create vectors using fmalloc}
#'   \item{\code{\link{cleanup_fmalloc}}}{Clean up fmalloc resources}
#' }
#'
#' @section Features:
#' \itemize{
#'   \item Persistent storage of R vectors in files
#'   \item Memory-efficient handling of large datasets
#'   \item ALTREP integration for seamless R vector operations
#'   \item Cross-platform support (Linux, macOS, Windows)
#' }
#'
#' @docType package
#' @name Rfmalloc-package
#' @aliases Rfmalloc
#' @useDynLib Rfmalloc, .registration=TRUE
"_PACKAGE"
