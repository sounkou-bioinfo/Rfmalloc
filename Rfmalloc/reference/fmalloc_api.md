# Public Rfmalloc API helpers

Introspection and lifecycle helpers for fmalloc runtime handles and
fmalloc ALTREP vectors. These functions are thin R wrappers around the
package's native registered routines and mirror the installed C-callable
API.

## Usage

``` r
fmalloc_default_runtime()

is_fmalloc_runtime(x)

is_fmalloc_vector(x)

fmalloc_runtime(x)

fmalloc_runtime_info(runtime = NULL)

fmalloc_vector_info(x)

fmalloc_vector_type(x, label = TRUE)

fmalloc_vector_length(x)

fmalloc_vector_payload_ptr(x)
```

## Arguments

- x:

  An object to test or inspect. For vector helpers, `x` must be an
  active fmalloc ALTREP vector unless the function name starts with
  `is_`.

- runtime:

  Optional runtime handle returned by
  [`open_fmalloc()`](https://sounkou-bioinfo.github.io/Rfmalloc/Rfmalloc/reference/open_fmalloc.md).
  If not supplied, the current default runtime from
  [`init_fmalloc()`](https://sounkou-bioinfo.github.io/Rfmalloc/Rfmalloc/reference/init_fmalloc.md)
  is used.

- label:

  Logical scalar. If TRUE, return an R type label such as `"integer"`;
  if FALSE, return the underlying R `SEXPTYPE` integer code.

## Value

Depends on the helper: logical predicates, runtime external pointers,
metadata lists, or payload external pointers.
