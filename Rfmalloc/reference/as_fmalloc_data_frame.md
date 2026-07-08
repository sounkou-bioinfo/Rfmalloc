# Convert to data.frame for fmalloc vectors

Thin convenience wrapper around
[`data.frame()`](https://rdrr.io/r/base/data.frame.html).

## Usage

``` r
as_fmalloc_data_frame(
  ...,
  row.names = NULL,
  check.names = TRUE,
  stringsAsFactors = FALSE
)
```

## Arguments

- ...:

  Columns or objects to include in the frame.

- row.names:

  Optional row names for the frame.

- check.names:

  Whether to enforce syntactic column names.

- stringsAsFactors:

  Deprecated: retained for compatibility.

## Value

A `data.frame` containing the supplied columns.
