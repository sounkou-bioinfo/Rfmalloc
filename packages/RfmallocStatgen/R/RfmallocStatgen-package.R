#' RfmallocStatgen: Statistical Genetics on Rfmalloc Genotype Tensors
#'
#' RfmallocStatgen is the statistical-genetics layer of the Rfmalloc stack:
#' it runs genome-scale methods over fmalloc-backed genotype tensors,
#' closing over 'bigsnpr's analysis stack on denser storage with native
#' readers, and adding the haplotype methods 'bigsnpr' cannot. See
#' \code{ROADMAP.md} in the package source (or the package README) for the
#' active research programme. Implemented methods cover streamed PCA and
#' regression, banded LD construction, LDpred2, and matrix-form
#' colocalisation.
#'
#' @section Validation policy:
#' Every method is validated against 'bigsnpr'/'bigstatsr' as the reference
#' oracle. The methods are implemented clean room from the published
#' algorithms: both those packages and this one are GPL-3, so nothing is
#' ever lifted from their source, only compared against its output.
#'
#' @keywords internal
#' @import Rfmalloc
"_PACKAGE"
