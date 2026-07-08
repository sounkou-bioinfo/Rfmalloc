# PLINK 1 genotypes as a 2-bit fmalloc tensor

Encodes an integer dosage matrix (`0`, `1`, `2`, `NA`) into PLINK 1
`.bed` bit packing and stores it in fmalloc-backed, memory-mapped
storage as an
[fmalloc_tensor](https://sounkou-bioinfo.github.io/Rfmalloc/Rfmalloc/reference/fmalloc_tensor.md)
of codec `"bed"`. Samples are rows, variants are columns, matching
`.bed`'s SNP-major layout: a variant is a contiguous column.

## Usage

``` r
fmalloc_bed(x, runtime = NULL)
```

## Arguments

- x:

  An integer matrix of dosages of the first allele: `0`, `1`, `2`, or
  `NA`.

- runtime:

  Runtime handle from
  [`open_fmalloc()`](https://sounkou-bioinfo.github.io/Rfmalloc/Rfmalloc/reference/open_fmalloc.md);
  defaults to the runtime established by
  [`init_fmalloc()`](https://sounkou-bioinfo.github.io/Rfmalloc/Rfmalloc/reference/init_fmalloc.md).

## Value

An `fmalloc_tensor` of dtype `"bed"` with `dim(x)`.

## Details

Two bits per genotype. That is four times tighter than a
one-byte-per-genotype file-backed matrix, and thirty-two times tighter
than the doubles it decodes to. Products against the tensor decode
bounded column panels on the fly and contract them with BLAS, so the
genotypes are never materialized as doubles.

Missing genotypes decode to `NA_real_`, which the matrix-product path
does not impute; standardize or impute before multiplying.

## See also

[`create_fmalloc_tensor()`](https://sounkou-bioinfo.github.io/Rfmalloc/Rfmalloc/reference/fmalloc_tensor.md),
[`fmalloc_tensor_materialize()`](https://sounkou-bioinfo.github.io/Rfmalloc/Rfmalloc/reference/fmalloc_tensor.md)

## Examples

``` r
rt <- open_fmalloc(tempfile(), size_gb = 0.1)
g <- matrix(c(0L, 1L, 2L, NA_integer_, 2L, 0L), nrow = 3, ncol = 2)
tn <- fmalloc_bed(g, runtime = rt)
fmalloc_tensor_materialize(tn)
#>      [,1] [,2]
#> [1,]    0   NA
#> [2,]    1    2
#> [3,]    2    0
#> attr(,"class")
#> [1] "fmalloc_matrix" "matrix"         "fmalloc"        "numeric"       
cleanup_fmalloc(rt)
```
