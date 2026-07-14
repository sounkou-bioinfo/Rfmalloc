#' Rpgen: PLINK 2 genotype ingestion into file-backed storage
#'
#' Rpgen uses 'PLINK 2's pgenlib and native importers
#' (\url{https://github.com/chrchang/plink-ng}) to read its genotype format
#' family, including its legacy and interchange formats, into bounded,
#' file-backed 'Rfmalloc' layouts. The main R entry point is
#' \code{\link{rpgen_ingest}}. The native readers are also exposed through
#' \code{R_RegisterCCallable()} for sibling packages.
#'
#' @section Why not just use pgenlibr:
#' 'pgenlibr' only exposes an R-level (Rcpp) interface: nothing outside it
#' can link against its pgenlib build. Rpgen vendors the same sources so
#' other R packages (starting with the 'Rfmalloc' ecosystem) can read
#' \code{.pgen} genotypes natively from their own C or C++ code.
#'
#' @section For downstream package authors:
#' Add \code{Rpgen} to \code{LinkingTo} (and \code{Imports}, so the
#' namespace is guaranteed loaded first) in your package's
#' \code{DESCRIPTION}, then \code{#include <Rpgen.h>} in your C/C++ source
#' and resolve the C-callables you need with \code{R_GetCCallable()}.
#'
#' @section Record transfer:
#' PGEN and BED keep one pgenlib reader open and transfer bounded variant
#' panels into an opaque 'Rfmalloc' buffer context. The source declares
#' hardcalls, dosages, or phase bits; 'Rfmalloc' owns allocation, packing,
#' alignment, and the persistent layout.
#'
#' @keywords internal
#' @useDynLib Rpgen, .registration = TRUE
#' @import Rfmalloc
"_PACKAGE"
