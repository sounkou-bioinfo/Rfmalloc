# Correlation between two variants of a banded LD store

Returns `r[i, j]` from a banded LD store. Pairs outside the stored band
(too far apart to be in each other's window) return `0`.

## Usage

``` r
ld_pair(store, i, j)
```

## Arguments

- store:

  An
  [fmalloc_ld](https://sounkou-bioinfo.github.io/Rfmalloc/Rfmalloc/reference/fmalloc_ld.md)
  object.

- i, j:

  1-based variant indices.

## Value

The (quantized) correlation `r[i, j]`, or `0` if the pair is outside the
band.

## See also

[`fmalloc_ld()`](https://sounkou-bioinfo.github.io/Rfmalloc/Rfmalloc/reference/fmalloc_ld.md),
[`ld_col()`](https://sounkou-bioinfo.github.io/Rfmalloc/Rfmalloc/reference/ld_col.md),
[`ld_ncol()`](https://sounkou-bioinfo.github.io/Rfmalloc/Rfmalloc/reference/ld_ncol.md)
