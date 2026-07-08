# Construct data.frame from fmalloc columns

Thin constructor wrapper around
[`data.frame()`](https://rdrr.io/r/base/data.frame.html) that keeps
fmalloc vectors as column payloads.

## Usage

``` r
create_fmalloc_data_frame(
  ...,
  row.names = NULL,
  check.names = TRUE,
  stringsAsFactors = FALSE
)
```

## Arguments

- ...:

  Columns to include in the frame.

- row.names:

  Optional row names for the frame.

- check.names:

  Whether to enforce syntactic column names.

- stringsAsFactors:

  Deprecated: retained for compatibility.

## Value

A `data.frame` with the provided columns.

## Examples

``` r
if (FALSE) { # \dontrun{
rt <- open_fmalloc(tempfile(fileext = ".bin"))
x <- create_fmalloc_vector("integer", 3, runtime = rt)
y <- create_fmalloc_vector("character", 3, runtime = rt)
x[] <- 1:3
y[] <- c("a", "b", "c")
df <- create_fmalloc_data_frame(x = x, y = y)
cleanup_fmalloc(rt)
} # }
```
