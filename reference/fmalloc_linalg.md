# Matrix algebra for fmalloc-backed vectors and matrices

Provides fmalloc-aware matrix products for `%*%`,
[`crossprod()`](https://rdrr.io/r/base/crossprod.html), and
[`tcrossprod()`](https://rdrr.io/r/base/crossprod.html). Results are
allocated in the runtime of the first fmalloc operand and returned as
fmalloc-backed matrices.

## Usage

``` r
# S3 method for class 'fmalloc'
x %*% y

# S3 method for class 'fmalloc'
crossprod(x, y = NULL, ...)

# S3 method for class 'fmalloc'
tcrossprod(x, y = NULL, ...)

# S3 method for class 'fmalloc'
matrixOps(x, y)
```

## Arguments

- x, y:

  Numeric, logical, or complex vectors/matrices. At least one operand
  must be fmalloc-backed for these methods to dispatch.

- ...:

  Unused.

## Value

An fmalloc-backed numeric or complex matrix.

## Details

These methods use native C kernels to keep computations in
package-managed fmalloc storage while preserving base R behavior for
matrix products.
