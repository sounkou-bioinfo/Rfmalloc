# Create Matrix Using fmalloc

Creates an fmalloc-backed ALTREP matrix in a single step by allocating
vector storage and installing matrix dimensions (and optional dimnames).

## Usage

``` r
create_fmalloc_matrix(
  type = "integer",
  nrow,
  ncol,
  dimnames = NULL,
  runtime = NULL,
  zero_initialize = TRUE
)
```

## Arguments

- type:

  Character string specifying the vector type. Supported values are the
  same as for
  [`create_fmalloc_vector()`](https://sounkou-bioinfo.github.io/Rfmalloc/Rfmalloc/reference/create_fmalloc_vector.md).

- nrow:

  Integer number of rows.

- ncol:

  Integer number of columns.

- dimnames:

  Optional `dimnames` list for the matrix.

- runtime:

  Optional runtime handle returned by
  [`open_fmalloc()`](https://sounkou-bioinfo.github.io/Rfmalloc/Rfmalloc/reference/open_fmalloc.md).
  If not supplied, the default runtime established by
  [`init_fmalloc()`](https://sounkou-bioinfo.github.io/Rfmalloc/Rfmalloc/reference/init_fmalloc.md)
  is used.

- zero_initialize:

  Logical scalar passed through to payload allocation. See
  [`create_fmalloc_vector()`](https://sounkou-bioinfo.github.io/Rfmalloc/Rfmalloc/reference/create_fmalloc_vector.md)
  for semantics.

## Value

An fmalloc-backed ALTREP matrix object.

## Examples

``` r
if (FALSE) { # \dontrun{
rt <- open_fmalloc(tempfile(fileext = ".bin"))
m <- create_fmalloc_matrix("integer", nrow = 2, ncol = 3, runtime = rt)
cleanup_fmalloc(rt)
} # }
```
