# Initialize fmalloc

Compatibility wrapper that opens an fmalloc runtime and installs it as
the package default runtime used by
[`create_fmalloc_vector()`](https://sounkou-bioinfo.github.io/Rfmalloc/reference/create_fmalloc_vector.md)
when no explicit runtime is supplied.

## Usage

``` r
init_fmalloc(filepath, size_gb = NULL, mode = c("persistent", "scratch"))
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

Logical indicating whether the file was newly initialized.

## Details

For new code, prefer
[`open_fmalloc()`](https://sounkou-bioinfo.github.io/Rfmalloc/reference/open_fmalloc.md)
and pass the returned runtime handle to
[`create_fmalloc_vector()`](https://sounkou-bioinfo.github.io/Rfmalloc/reference/create_fmalloc_vector.md).

## Examples

``` r
if (FALSE) { # \dontrun{
alloc_file <- tempfile(fileext = ".bin")
init_fmalloc(alloc_file)
v <- create_fmalloc_vector("integer", 1000)
cleanup_fmalloc()
unlink(alloc_file)
} # }
```
