# Column / row variances of an fmalloc matrix

Per-column (`fmalloc_colVars`) or per-row (`fmalloc_rowVars`) sample
variances, for highly-variable-feature selection and QC. Computed from
the fmalloc reduction kernels (`colMeans`/`rowMeans` of `X` and `X^2`),
so the result stays out-of-core-friendly and never materializes an
ordinary R copy of `X`.

## Usage

``` r
fmalloc_colVars(X)

fmalloc_rowVars(X)
```

## Arguments

- X:

  An fmalloc-backed numeric matrix.

## Value

A numeric vector of length `ncol(X)` (`fmalloc_colVars`) or `nrow(X)`
(`fmalloc_rowVars`).
