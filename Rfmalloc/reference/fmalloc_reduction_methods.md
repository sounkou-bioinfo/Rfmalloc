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

## Details

These implementations keep managed execution for 2D `fmalloc` matrices
with `dims = 1L`. For unsupported shapes or `dims` values (for example,
non-2D arrays or `dims != 1L`), the methods warn and delegate to the
base R implementations
([`base::rowSums`](https://rdrr.io/r/base/colSums.html),
[`base::colSums`](https://rdrr.io/r/base/colSums.html),
[`base::rowMeans`](https://rdrr.io/r/base/colSums.html), and
[`base::colMeans`](https://rdrr.io/r/base/colSums.html)).
