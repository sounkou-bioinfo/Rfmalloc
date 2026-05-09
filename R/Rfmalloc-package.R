#' Rfmalloc: Memory-Mapped File Allocation for R
#'
#' Rfmalloc provides experimental memory-mapped file allocation capabilities for
#' R using a patched copy of the fmalloc library. The current package exposes
#' ALTREP file-backed vector allocation for logical, integer, numeric, raw,
#' complex, character, and list vectors with fmalloc payload storage.
#'
#' @section Main Functions:
#' \describe{
#'   \item{\code{\link{open_fmalloc}}}{Open an explicit fmalloc runtime handle.}
#'   \item{\code{\link{init_fmalloc}}}{Open and install a default fmalloc runtime.}
#'   \item{\code{\link{create_fmalloc_vector}}}{Create vectors using fmalloc.}
#'   \item{\code{\link{cleanup_fmalloc}}}{Request cleanup of an fmalloc runtime.}
#' }
#'
#' @section Current Scope:
#' \itemize{
#'   \item ALTREP file-backed allocation for logical, integer, numeric, raw,
#'         complex, character, and list vectors.
#'   \item Large allocations spanning multiple fmalloc chunks.
#'   \item Multiple runtime handles in one R process.
#'   \item Persistent and scratch runtime modes.
#'   \item Reference serialization for persistent fixed-width atomic and
#'         character ALTREP vectors.
#'   \item Native lifetime tracking so runtime mappings outlive reachable
#'         vectors allocated from them.
#' }
#'
#' @section Future Work:
#' Future work includes a complete persistent allocation catalog,
#' fmalloc-backed subset results, and richer persistence semantics for
#' pointer-containing vector types.
#'
#' @docType package
#' @name Rfmalloc-package
#' @aliases Rfmalloc
#' @useDynLib Rfmalloc, .registration=TRUE
"_PACKAGE"
