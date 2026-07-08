# List GGUF Tensors

Lists the tensor directory of a GGUF file as a data frame, without
reading or dequantizing any tensor payload.

## Usage

``` r
gguf_tensors(x)
```

## Arguments

- x:

  Either a path to a GGUF file, or a `"gguf_ctx"` object returned by
  [`gguf_open()`](https://sounkou-bioinfo.github.io/Rfmalloc/Rgguf/reference/gguf_open.md).

## Value

A data frame with one row per tensor and columns:

- name:

  Tensor name.

- type:

  GGUF tensor type name, e.g. `"f32"`, `"q4_k"`.

- n_dims:

  Number of dimensions.

- dims:

  A list column; `dims[[i]]` is an integer vector of tensor `i`'s
  dimensions, GGUF-order (`dim[0]` first, the fastest-varying/
  contiguous dimension, which
  [`gguf_tensor()`](https://sounkou-bioinfo.github.io/Rfmalloc/Rgguf/reference/gguf_tensor.md)
  maps to R's first, column-major dimension).

- n_elements:

  Total number of scalar elements.

- nbytes:

  Size of the tensor's raw (possibly quantized) on-disk representation,
  in bytes.

- offset:

  Byte offset of the tensor data from the start of the file.

## Examples

``` r
tmp <- tempfile(fileext = ".gguf")
gguf_write_tensors(tmp, list(w = matrix(1:6 + 0.5, nrow = 2), b = 1:3 + 0.5))
gguf_tensors(tmp)
#>   name type n_dims dims n_elements nbytes offset
#> 1    w  f32      2 2, 3          6     24    128
#> 2    b  f32      1    3          3     12    160
unlink(tmp)
```
