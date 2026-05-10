# Matrix reduction helpers for fmalloc-backed matrices

These S3 methods preserve current fmalloc behavior for matrix
summary/reduction operations while returning ordinary R vectors for
small results.

## Usage

``` r
rowSums(x, na.rm = FALSE, dims = 1L)

colSums(x, na.rm = FALSE, dims = 1L)

rowMeans(x, na.rm = FALSE, dims = 1L)

colMeans(x, na.rm = FALSE, dims = 1L)
```

## Arguments

- x:

  A matrix-like object.

- na.rm:

  Logical scalar controlling NA removal.

- dims:

  Numeric scalar for dimensions.

## Value

The reduction result, as either an ordinary R object or a `fmalloc`
vector when result length exceeds
`getOption("Rfmalloc.reduce_result_length", 1e6)`.
