# Write a Minimal GGUF File

Writes a named list of numeric vectors/matrices/arrays to a GGUF file as
32-bit floating point (`f32`) tensors, with optional simple metadata.
This is a minimal writer, primarily meant to build small GGUF fixtures
for this package's own round-trip tests without shipping binary test
fixtures, but it is exported since it is also useful on its own for
producing test fixtures for other GGUF consumers.

## Usage

``` r
gguf_write_tensors(path, tensors, metadata = list())
```

## Arguments

- path:

  Character string giving the output file path.

- tensors:

  A non-empty named list of numeric vectors, matrices, or arrays. Names
  become tensor names and must be unique and non-empty.

- metadata:

  A named list of metadata key-value pairs to write. Each value must be
  a single (length-1), non-missing string or numeric value; numeric
  values are written as 64-bit floats (`FLOAT64`). Defaults to
  [`list()`](https://rdrr.io/r/base/list.html) (no metadata).

## Value

`path`, invisibly.

## Details

Tensor dimensions are taken from each object's
[`dim()`](https://rdrr.io/r/base/dim.html) (or its
[`length()`](https://rdrr.io/r/base/length.html) for a plain vector,
written as a 1-dimensional tensor) and stored in GGUF
`dim[0]`-fastest-varying order directly from R's column-major storage,
so
[`gguf_tensor()`](https://sounkou-bioinfo.github.io/Rfmalloc/Rgguf/reference/gguf_tensor.md)/[`gguf_import()`](https://sounkou-bioinfo.github.io/Rfmalloc/Rgguf/reference/gguf_import.md)
read back exactly the matrix/array you wrote (see
`inst/tinytest/test_gguf_roundtrip.R`).

Existing files at `path` are silently overwritten.

## Examples

``` r
tmp <- tempfile(fileext = ".gguf")
gguf_write_tensors(tmp,
    tensors = list(weight = matrix(1:6 + 0.5, nrow = 2)),
    metadata = list(name = "example-model", version = 1)
)
gguf_tensors(tmp)
#>     name type n_dims dims n_elements nbytes offset
#> 1 weight  f32      2 2, 3          6     24    160
unlink(tmp)
```
