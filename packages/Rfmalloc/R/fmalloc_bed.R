#' PLINK 1 genotypes as a 2-bit fmalloc tensor
#'
#' Encodes an integer dosage matrix (`0`, `1`, `2`, `NA`) into PLINK 1 `.bed`
#' bit packing and stores it in fmalloc-backed, memory-mapped storage as an
#' [fmalloc_tensor] of codec `"bed"`. Samples are rows, variants are columns,
#' matching `.bed`'s SNP-major layout: a variant is a contiguous column.
#'
#' Two bits per genotype. That is four times tighter than a one-byte-per-genotype
#' file-backed matrix, and thirty-two times tighter than the doubles it decodes
#' to. Products against the tensor decode bounded column panels on the fly and
#' contract them with BLAS, so the genotypes are never materialized as doubles.
#'
#' Missing genotypes decode to `NA_real_`, which the matrix-product path does not
#' impute; standardize or impute before multiplying.
#'
#' @param x An integer matrix of dosages of the first allele: `0`, `1`, `2`, or
#'   `NA`.
#' @param runtime Runtime handle from [open_fmalloc()]; defaults to the runtime
#'   established by [init_fmalloc()].
#'
#' @return An `fmalloc_tensor` of dtype `"bed"` with `dim(x)`.
#'
#' @seealso [create_fmalloc_tensor()], [fmalloc_tensor_materialize()]
#' @examples
#' rt <- open_fmalloc(tempfile(), size_gb = 0.1)
#' g <- matrix(c(0L, 1L, 2L, NA_integer_, 2L, 0L), nrow = 3, ncol = 2)
#' tn <- fmalloc_bed(g, runtime = rt)
#' fmalloc_tensor_materialize(tn)
#' cleanup_fmalloc(rt)
#' @export
fmalloc_bed <- function(x, runtime = NULL) {
    runtime <- .fmalloc_get_runtime(runtime)
    if (!is.matrix(x) || !is.integer(x)) {
        stop("x must be an integer matrix of dosages (0, 1, 2, NA)")
    }
    payload <- .Call("rfm_tensor_bed_encode_impl", x, runtime)
    create_fmalloc_tensor(payload, dtype = "bed", dim = dim(x))
}
