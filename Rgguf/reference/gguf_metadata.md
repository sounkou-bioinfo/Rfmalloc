# Read GGUF Metadata

Reads every metadata key-value pair from a GGUF file into a named R
list.

## Usage

``` r
gguf_metadata(x)
```

## Arguments

- x:

  Either a path to a GGUF file, or a `"gguf_ctx"` object returned by
  [`gguf_open()`](https://sounkou-bioinfo.github.io/Rfmalloc/Rgguf/reference/gguf_open.md).

## Value

A named list of metadata values, in file order.

## Details

Scalar values are converted to the closest native R type (`integer` for
8/16/32-bit signed and unsigned-but-int-safe integers, `numeric` for
32-bit unsigned and 64-bit integers/floats, `logical` for booleans, and
`character` for strings). Arrays of a supported scalar type become an R
vector of that type. A metadata value of a type this package does not
represent (there are none in the current GGUF spec, but the check is
defensive) is returned as `NULL` while keeping its name, rather than
raising an error.

## Examples

``` r
tmp <- tempfile(fileext = ".gguf")
gguf_write_tensors(tmp, list(w = matrix(1:6 + 0.5, nrow = 2)),
    metadata = list(name = "example", version = 1))
gguf_metadata(tmp)
#> $name
#> [1] "example"
#> 
#> $version
#> [1] 1
#> 
unlink(tmp)
```
