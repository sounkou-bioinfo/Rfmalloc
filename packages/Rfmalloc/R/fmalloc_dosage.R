#' Fractional genotype dosages as a 1-byte fmalloc tensor
#'
#' Encodes a numeric matrix of dosages (values in `[0, 2]`, `NA` allowed) into
#' fixed-point single-byte storage as an [fmalloc_tensor] of codec `"dosage"`,
#' the continuous sibling of [fmalloc_bed()]. Samples are rows, variants are
#' columns. A dosage `d` is stored as `round(d * 127)` (`0..254`), with `255`
#' reserved for missing, so the resolution is `2 / 254`. This is lossy by
#' design: it is a storage codec, eight times tighter than the doubles it decodes
#' to, and it is the target a PLINK 2 `.pgen` dosage import re-encodes into.
#'
#' Products against the tensor decode bounded column panels and contract them
#' with BLAS, so dosages are never materialized as doubles.
#'
#' @param x A numeric matrix of dosages in `[0, 2]`, with `NA` for missing.
#' @param runtime Runtime handle from [open_fmalloc()]; defaults to the runtime
#'   established by [init_fmalloc()].
#'
#' @return An `fmalloc_tensor` of dtype `"dosage"` with `dim(x)`.
#'
#' @seealso [fmalloc_bed()], [fmalloc_dosage_standardize()]
#' @examples
#' rt <- open_fmalloc(tempfile(), size_gb = 0.1)
#' d <- matrix(c(0, 0.3, 1.7, NA, 2, 0.9), nrow = 3, ncol = 2)
#' tn <- fmalloc_dosage(d, runtime = rt)
#' round(fmalloc_tensor_materialize(tn), 2)
#' cleanup_fmalloc(rt)
#' @export
fmalloc_dosage <- function(x, runtime = NULL) {
    runtime <- .fmalloc_get_runtime(runtime)
    if (!is.matrix(x) || !is.numeric(x)) {
        stop("x must be a numeric matrix of dosages in [0, 2] (or NA)")
    }
    storage.mode(x) <- "double"
    payload <- .Call("rfm_tensor_dosage_encode_impl", x, runtime)
    create_fmalloc_tensor(payload, dtype = "dosage", dim = dim(x))
}

#' Bake per-variant standardization into a dosage tensor
#'
#' Computes each variant's mean and standard deviation in one streaming pass and
#' stores them in the tensor, so that every subsequent decode returns
#' standardized, mean-imputed values: missing dosages decode to the variant mean,
#' hence to `0` after centering. Products against the standardized tensor are
#' therefore centered-and-scaled with no dosage ever materialized as a double and
#' no second pass, which is what a dosage-based PCA or GRM needs. The mirror of
#' [fmalloc_bed_standardize()] for continuous dosages.
#'
#' @param x A `"dosage"` [fmalloc_tensor] from [fmalloc_dosage()] (raw, not
#'   already standardized).
#' @param scale One of `"sd"` (default; the sample standard deviation of the
#'   mean-imputed column, matching [scale()]) or `"binomial"`
#'   (`sqrt(2 p (1 - p))`, `p = mean/2`).
#' @param runtime Runtime handle from [open_fmalloc()]; defaults to the runtime
#'   established by [init_fmalloc()].
#'
#' @return A `"dosage"` `fmalloc_tensor` whose decode is standardized.
#'   Monomorphic variants (zero variance) decode to `0`.
#'
#' @seealso [fmalloc_dosage()], [fmalloc_bed_standardize()]
#' @examples
#' rt <- open_fmalloc(tempfile(), size_gb = 0.1)
#' d <- matrix(c(0, 0.3, 1.7, 2, 1.1, 0.9), nrow = 3, ncol = 2)
#' tn <- fmalloc_dosage_standardize(fmalloc_dosage(d, runtime = rt), runtime = rt)
#' round(fmalloc_tensor_materialize(tn), 3)
#' cleanup_fmalloc(rt)
#' @export
fmalloc_dosage_standardize <- function(x, scale = c("sd", "binomial"),
                                       runtime = NULL) {
    if (!inherits(x, "fmalloc_tensor") ||
        !identical(attr(x, "rfm_dtype"), "dosage")) {
        stop("x must be a 'dosage' fmalloc_tensor")
    }
    scale <- match.arg(scale)
    runtime <- .fmalloc_get_runtime(runtime)
    mode <- if (scale == "binomial") 1L else 0L
    payload <- .Call("rfm_tensor_dosage_standardize_impl", x, runtime, mode)
    create_fmalloc_tensor(payload, dtype = "dosage", dim = attr(x, "rfm_dims"))
}
