# Fractional genotype dosages as a 1-byte fmalloc tensor

Encodes a numeric matrix of dosages (values in `[0, 2]`, `NA` allowed)
into fixed-point single-byte storage as an
[fmalloc_tensor](https://sounkou-bioinfo.github.io/Rfmalloc/Rfmalloc/reference/fmalloc_tensor.md)
of codec `"dosage"`, the continuous sibling of
[`fmalloc_bed()`](https://sounkou-bioinfo.github.io/Rfmalloc/Rfmalloc/reference/fmalloc_bed.md).
Samples are rows, variants are columns. A dosage `d` is stored as
`round(d * 127)` (`0..254`), with `255` reserved for missing, so the
resolution is `2 / 254`. This is lossy by design: it is a storage codec,
eight times tighter than the doubles it decodes to, and it is the target
a PLINK 2 `.pgen` dosage import re-encodes into.

## Usage

``` r
fmalloc_dosage(x, runtime = NULL)
```

## Arguments

- x:

  A numeric matrix of dosages in `[0, 2]`, with `NA` for missing.

- runtime:

  Runtime handle from
  [`open_fmalloc()`](https://sounkou-bioinfo.github.io/Rfmalloc/Rfmalloc/reference/open_fmalloc.md);
  defaults to the runtime established by
  [`init_fmalloc()`](https://sounkou-bioinfo.github.io/Rfmalloc/Rfmalloc/reference/init_fmalloc.md).

## Value

An `fmalloc_tensor` of dtype `"dosage"` with `dim(x)`.

## Details

Products against the tensor decode bounded column panels and contract
them with BLAS, so dosages are never materialized as doubles.

## See also

[`fmalloc_bed()`](https://sounkou-bioinfo.github.io/Rfmalloc/Rfmalloc/reference/fmalloc_bed.md),
[`fmalloc_dosage_standardize()`](https://sounkou-bioinfo.github.io/Rfmalloc/Rfmalloc/reference/fmalloc_dosage_standardize.md)

## Examples

``` r
rt <- open_fmalloc(tempfile(), size_gb = 0.1)
d <- matrix(c(0, 0.3, 1.7, NA, 2, 0.9), nrow = 3, ncol = 2)
tn <- fmalloc_dosage(d, runtime = rt)
round(fmalloc_tensor_materialize(tn), 2)
#> [1]  0  0  2 NA  2  1
#> attr(,"class")
#> [1] "fmalloc_vector" "fmalloc"        "numeric"       
cleanup_fmalloc(rt)
```
