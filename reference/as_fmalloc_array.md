# Convert a vector to fmalloc array metadata

Returns an existing vector re-typed as an array by installing array
dimensions (and optional dimnames) as metadata.

## Usage

``` r
as_fmalloc_array(x, dim = NULL, dimnames = NULL, copy = TRUE)
```

## Arguments

- x:

  A vector.

- dim:

  Target dimension vector.

- dimnames:

  Optional `dimnames` for the resulting array.

- copy:

  If TRUE (default), allocate a new fmalloc-backed array object. If
  FALSE, install metadata in place on the same fmalloc ALTREP payload
  without allocation (this also updates any aliases of `x`).

## Value

An array object, backed by the same payload when `copy = FALSE`.
