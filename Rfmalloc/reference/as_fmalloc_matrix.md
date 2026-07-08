# Convert a vector to fmalloc matrix metadata

Returns an existing vector re-typed as a matrix by installing matrix
dimensions (and optional dimnames) as metadata.

## Usage

``` r
as_fmalloc_matrix(x, nrow = NULL, ncol = NULL, dimnames = NULL, copy = TRUE)
```

## Arguments

- x:

  A vector.

- nrow:

  Optional target row count.

- ncol:

  Optional target column count.

- dimnames:

  Optional `dimnames` for the resulting matrix.

- copy:

  If TRUE (default), allocate a new fmalloc-backed matrix object. If
  FALSE, install metadata in place on the same fmalloc ALTREP payload
  without allocation (this also updates any aliases of `x`).

## Value

A matrix object, backed by the same payload when `copy = FALSE`.
