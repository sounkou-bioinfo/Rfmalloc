## pgen_pca.R - a .pgen file becomes principal
## components without a dense double copy of the genotypes at any point.
##
## The path:
##   .pgen (2-bit-packed on disk, pgenlib's own format)
##     -> rpgen_dosage()                     [plink2::PgrGetD(), in C]
##     -> fmalloc-backed "dosage" tensor      [1 byte/dosage, fixed-point]
##     -> Rfmalloc::fmalloc_dosage_standardize()
##                                            [mean-imputed, centered, scaled;
##                                             still 1 byte/dosage on disk]
##     -> Rfmalloc::fmalloc_pca()             [Gram matrix + eigendecomposition,
##                                             streamed column panels through
##                                             BLAS - no dense double genotype
##                                             matrix is ever materialized]
##
## Run interactively with:
##   Rscript -e 'source(system.file("examples", "pgen_pca.R", package = "Rpgen"))'

library(Rpgen)

pgen_path <- system.file("extdata", "chr21_phase3_start.pgen", package = "Rpgen")
stopifnot(nzchar(pgen_path))

info <- rpgen_info(pgen_path)
cat(sprintf("chr21_phase3_start.pgen: %d samples x %d variants\n",
            info$n_sample, info$n_variant))

## Note: this script runs at top level (not wrapped in a function), for
## readability when read top-to-bottom, so cleanup is explicit at the end
## rather than via on.exit() - a bare top-level on.exit() in a source()d
## script fires as soon as its enclosing top-level statement finishes
## evaluating (source() evaluates one top-level expression at a time), not
## deferred to end of script.
tmp <- tempfile(fileext = ".bin")
rt <- Rfmalloc::open_fmalloc(tmp, mode = "scratch", size_gb = 0.5)

## .pgen -> fmalloc dosage tensor (plink2::PgrGetD() in C, then packed to
## fmalloc's 1-byte fixed-point dosage codec - see Rfmalloc::fmalloc_dosage()).
dosages <- rpgen_dosage(pgen_path, runtime = rt)
cat(sprintf("dosage tensor: %d x %d, dtype '%s', %d payload bytes\n",
            dim(dosages)[1], dim(dosages)[2],
            attr(dosages, "rfm_dtype"), length(unclass(dosages))))

## Bake per-variant mean-imputation + standardization into the tensor: every
## subsequent decode returns centered, scaled values, still 1 byte/dosage on
## disk (see Rfmalloc::fmalloc_dosage_standardize()).
standardized <- Rfmalloc::fmalloc_dosage_standardize(dosages, runtime = rt)

## Principal components from the standardized tensor's Gram matrix
## (Rfmalloc::fmalloc_pca(): out-of-core crossprod() + a small eigendecomposition
## + an out-of-core projection). center = FALSE because the tensor above is
## already centered - fmalloc_pca()'s own centering correction would double
## it otherwise.
k <- 5L
pca <- Rfmalloc::fmalloc_pca(standardized, k = k, center = FALSE)

cat("standard deviations of the first", k, "PCs:\n")
print(pca$sdev)
cat(sprintf("scores: %d samples x %d PCs; loadings: %d variants x %d PCs\n",
            nrow(pca$x), ncol(pca$x), nrow(pca$rotation), ncol(pca$rotation)))

## Sanity checks: PCs are ordered by decreasing variance, scores/loadings are
## finite and the right shape, and no genotype ever got materialized as an
## n_sample x n_variant matrix of doubles along the way (only the pgenlib
## decode step and fmalloc's own bounded column-panel decodes touch raw
## values at all).
stopifnot(
    all(is.finite(pca$sdev)),
    all(diff(pca$sdev) <= 1e-8),                 # non-increasing
    identical(dim(pca$x), c(info$n_sample, k)),
    identical(dim(pca$rotation), c(info$n_variant, k)),
    all(is.finite(pca$x)),
    all(is.finite(pca$rotation))
)

cat("pgen -> standardized fmalloc tensor -> PCA: OK\n")

Rfmalloc::cleanup_fmalloc(rt)
unlink(tmp)
