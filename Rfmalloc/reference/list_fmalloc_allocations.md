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
  [`open_fmalloc()`](https://sounkou-bioinfo.github.io/Rfmalloc/Rfmalloc/reference/open_fmalloc.md).
  If not supplied, the default runtime established by
  [`init_fmalloc()`](https://sounkou-bioinfo.github.io/Rfmalloc/Rfmalloc/reference/init_fmalloc.md)
  is used.

## Value

A data frame with one row per catalog record and columns describing the
catalog record offset, generation, state, vector type, length, payload
offset, payload byte size, flags, and whether the record is recoverable
by reference serialization.

## Details

For successful recovery, look at the `state` column:

- `"committed"`: valid serialized payload exists for that record;

- `"tombstone"`: the payload has been destroyed and is non-recoverable
  unless the runtime remains open and referenced directly by an existing
  SEXP;

- other transient states are internal and are generally not expected.

`recoverable` indicates whether the record can be reopened via
serialized reference metadata. `payload_offset == 0` or
`payload_nbytes == 0` generally indicates a non-payload entry.

## Examples

``` r
if (FALSE) { # \dontrun{
rt <- open_fmalloc(tempfile(fileext = ".bin"))
v <- create_fmalloc_vector("integer", 10, runtime = rt)
list_fmalloc_allocations(rt)
cleanup_fmalloc(rt)
} # }
```
