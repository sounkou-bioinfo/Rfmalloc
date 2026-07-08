# Bake per-variant standardization into a bed tensor

Computes each variant's mean and standard deviation in one streaming
pass and stores them in the tensor, so that every subsequent decode
returns standardized, mean-imputed values: missing genotypes decode to
the variant mean, hence to `0` after centering. Products against the
standardized tensor are therefore centered-and-scaled with no genotype
ever materialized as a double and no second pass, which is what a
genotype PCA or GRM needs.

## Usage

``` r
fmalloc_bed_standardize(x, scale = c("sd", "binomial"), runtime = NULL)
```

## Arguments

- x:

  A `"bed"`
  [fmalloc_tensor](https://sounkou-bioinfo.github.io/Rfmalloc/Rfmalloc/reference/fmalloc_tensor.md)
  from
  [`fmalloc_bed()`](https://sounkou-bioinfo.github.io/Rfmalloc/Rfmalloc/reference/fmalloc_bed.md)
  (raw, not already standardized).

- scale:

  One of `"sd"` (default; the sample standard deviation of the
  mean-imputed column, matching
  [`scale()`](https://rdrr.io/r/base/scale.html)) or `"binomial"`
  (`sqrt(2 p (1 - p))`, `p = mean/2`, the allele-frequency scaling used
  by GRM / SmartPCA / GCTA).

- runtime:

  Runtime handle from
  [`open_fmalloc()`](https://sounkou-bioinfo.github.io/Rfmalloc/Rfmalloc/reference/open_fmalloc.md);
  defaults to the runtime established by
  [`init_fmalloc()`](https://sounkou-bioinfo.github.io/Rfmalloc/Rfmalloc/reference/init_fmalloc.md).

## Value

A `"bed"` `fmalloc_tensor` whose decode is standardized. Monomorphic
variants (zero variance) decode to `0`.

## See also

[`fmalloc_bed()`](https://sounkou-bioinfo.github.io/Rfmalloc/Rfmalloc/reference/fmalloc_bed.md)

## Examples

``` r
rt <- open_fmalloc(tempfile(), size_gb = 0.1)
g <- matrix(c(0L, 1L, 2L, 1L, 2L, 0L), nrow = 3, ncol = 2)
tn <- fmalloc_bed_standardize(fmalloc_bed(g, runtime = rt), runtime = rt)
round(fmalloc_tensor_materialize(tn), 3)
#> [1] -1  0  1  0  1 -1
#> attr(,"class")
#> [1] "fmalloc_vector" "fmalloc"        "numeric"       
cleanup_fmalloc(rt)
```
