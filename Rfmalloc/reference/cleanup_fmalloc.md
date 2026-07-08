# Clean Up fmalloc

Requests cleanup of an fmalloc runtime. If vectors allocated from the
runtime are still reachable, the native mapping is kept alive until
those vectors are garbage-collected.

## Usage

``` r
cleanup_fmalloc(runtime = NULL)
```

## Arguments

- runtime:

  Optional runtime handle returned by
  [`open_fmalloc()`](https://sounkou-bioinfo.github.io/Rfmalloc/Rfmalloc/reference/open_fmalloc.md).
  If not supplied, the current default runtime is cleaned up.

## Value

NULL (invisibly)

## Examples

``` r
if (FALSE) { # \dontrun{
init_fmalloc("data.bin")
v <- create_fmalloc_vector("integer", 100)
rm(v)
gc()
cleanup_fmalloc()
} # }
```
