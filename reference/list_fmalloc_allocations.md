# List Persistent fmalloc Allocations

Returns the in-file allocation catalog for a persistent fmalloc runtime.
The catalog is stored in the backing file and records physical
allocation metadata used to validate serialized persistent references.

## Usage

``` r
list_fmalloc_allocations(runtime = NULL)
```

## Arguments

- runtime:

  Optional runtime handle returned by
  [`open_fmalloc()`](https://sounkou-bioinfo.github.io/Rfmalloc/reference/open_fmalloc.md).
  If not supplied, the default runtime established by
  [`init_fmalloc()`](https://sounkou-bioinfo.github.io/Rfmalloc/reference/init_fmalloc.md)
  is used.

## Value

A data frame with one row per catalog record and columns describing the
catalog record offset, generation, state, vector type, length, payload
offset, payload byte size, flags, and whether the record is recoverable
by reference serialization.

## Examples

``` r
if (FALSE) { # \dontrun{
rt <- open_fmalloc(tempfile(fileext = ".bin"))
v <- create_fmalloc_vector("integer", 10, runtime = rt)
list_fmalloc_allocations(rt)
cleanup_fmalloc(rt)
} # }
```
