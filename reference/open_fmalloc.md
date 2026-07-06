# Open an fmalloc Runtime

Opens a file-backed fmalloc runtime and returns an external-pointer
handle. Multiple handles to the same path share the same underlying
runtime within a process. Runtime mode mismatches are rejected for an
already-open path. Runtime mode controls whether vector payloads are
durable persistent allocations or scratch allocations that can be
returned to fmalloc when their ALTREP handles are garbage-collected.

## Usage

``` r
open_fmalloc(filepath, size_gb = NULL, mode = c("persistent", "scratch"))
```

## Arguments

- filepath:

  Character string specifying the file path for fmalloc data.

- size_gb:

  Numeric value specifying the size of the backing file in GB
  (optional). If not specified, uses the package default size for new
  files or the existing file size.

- mode:

  Runtime mode. `"persistent"` keeps committed vector payloads in the
  backing file and serializes fixed-width atomic vectors by reference.
  `"scratch"` uses the backing file as a large temporary allocation
  arena and serializes vectors by value.

## Value

An external pointer of class `fmalloc_runtime`.
