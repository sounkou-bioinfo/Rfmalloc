# Create Vector Using fmalloc

Creates an ALTREP vector using a file-backed fmalloc runtime. The
returned object is ALTREP from creation time. Fixed-width atomic payload
bytes are allocated directly with fmalloc, and ALTREP duplication and
vector subsetting keep copy-on-write copies fmalloc-backed without using
R's non-API `Rf_allocVector3()` path.

## Usage

``` r
create_fmalloc_vector(
  type = "integer",
  length,
  runtime = NULL,
  zero_initialize = TRUE
)
```

## Arguments

- type:

  Character string specifying the vector type. Supported values are
  `"logical"`, `"integer"`, `"numeric"`/`"double"`, `"raw"`,
  `"complex"`, `"character"`, and `"list"`. Fixed-width atomic types
  expose a direct writable fmalloc `DATAPTR`; character vectors store
  string bytes in fmalloc and materialize R `CHARSXP` values on demand;
  list vectors use ALTREP element access with an R-visible reference
  sidecar for GC safety and only accept `NULL` or fmalloc-backed vectors
  from the same runtime as elements. Persistent list containers are
  serialized by nested reference states when all elements are
  recoverable from the same runtime.

- length:

  Integer specifying the non-negative length of the vector to create.

- runtime:

  Optional runtime handle returned by
  [`open_fmalloc()`](https://sounkou-bioinfo.github.io/Rfmalloc/reference/open_fmalloc.md).
  If not supplied, the default runtime established by
  [`init_fmalloc()`](https://sounkou-bioinfo.github.io/Rfmalloc/reference/init_fmalloc.md)
  is used.

- zero_initialize:

  Logical scalar. If TRUE (default), newly allocated payload bytes are
  zero-initialized. Set FALSE to skip initialization for faster large
  allocations when you will fully initialize values yourself.

## Value

A vector of the specified type and length, allocated using fmalloc.

## Examples

``` r
if (FALSE) { # \dontrun{
rt <- open_fmalloc(tempfile(fileext = ".bin"))
v <- create_fmalloc_vector("integer", 1000, runtime = rt)
cleanup_fmalloc(rt)
} # }
```
