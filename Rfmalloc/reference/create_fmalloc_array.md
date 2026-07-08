# Create Array Using fmalloc

Creates an fmalloc-backed ALTREP array in a single step by allocating
vector storage and installing array dimensions (and optional dimnames).

## Usage

``` r
create_fmalloc_array(
  type = "integer",
  dim,
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

- dim:

  Integer dimension vector.

- dimnames:

  Optional `dimnames` for the array.

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

An fmalloc-backed ALTREP array.

## Examples

``` r
if (FALSE) { # \dontrun{
rt <- open_fmalloc(tempfile(fileext = ".bin"))
a <- create_fmalloc_array("numeric", dim = c(2L, 3L), runtime = rt)
cleanup_fmalloc(rt)
} # }
```
