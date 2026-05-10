# Diagnose fmalloc runtime state

Returns diagnostic metadata for an open runtime handle, including
lightweight runtime attributes, the current allocation catalog, and a
catalog-level summary useful for estimating reclaimable/fragmented
payload regions.

## Usage

``` r
diagnose_fmalloc_runtime(runtime = NULL)
```

## Arguments

- runtime:

  Optional runtime handle returned by
  [`open_fmalloc()`](https://sounkou-bioinfo.github.io/Rfmalloc/reference/open_fmalloc.md).
  If not supplied, the current default runtime is used.

## Value

A named list with three components:

- `runtime`: runtime metadata such as file path, UUID, mode, catalog
  counters, live vectors, and reference state;

- `catalog`: the full allocation catalog returned by
  [`list_fmalloc_allocations()`](https://sounkou-bioinfo.github.io/Rfmalloc/reference/list_fmalloc_allocations.md);

- `summary`: a compact set of computed diagnostics and an explicit
  compaction status note.

## See also

[`list_fmalloc_allocations()`](https://sounkou-bioinfo.github.io/Rfmalloc/reference/list_fmalloc_allocations.md)

## Examples

``` r
if (FALSE) { # \dontrun{
rt <- open_fmalloc(tempfile(fileext = ".bin"), mode = "persistent")
x <- create_fmalloc_vector("integer", 4, runtime = rt)
y <- create_fmalloc_vector("logical", 2, runtime = rt)
diagnose_fmalloc_runtime(rt)
cleanup_fmalloc(rt)
} # }
```
