# Open a GGUF File

Opens a 'GGUF' model file and returns a lightweight context handle used
by
[`gguf_metadata()`](https://sounkou-bioinfo.github.io/Rfmalloc/Rgguf/reference/gguf_metadata.md),
[`gguf_tensors()`](https://sounkou-bioinfo.github.io/Rfmalloc/Rgguf/reference/gguf_tensors.md),
[`gguf_tensor()`](https://sounkou-bioinfo.github.io/Rfmalloc/Rgguf/reference/gguf_tensor.md),
and
[`gguf_import()`](https://sounkou-bioinfo.github.io/Rfmalloc/Rgguf/reference/gguf_import.md).
Metadata and the tensor directory are parsed by Rggml's official GGUF
implementation. Tensor bytes are mapped read-only and unmapped
automatically when the returned object is garbage collected, or earlier
if you call
[`gguf_import()`](https://sounkou-bioinfo.github.io/Rfmalloc/Rgguf/reference/gguf_import.md)/friends
with a plain file path (which open and close their own short-lived
context internally).

## Usage

``` r
gguf_open(path)
```

## Arguments

- path:

  Character string giving the path to a GGUF file.

## Value

An object of class `"gguf_ctx"`: an external pointer to the underlying
parser context, with a finalizer that closes it.

## Examples

``` r
tmp <- tempfile(fileext = ".gguf")
gguf_write_tensors(tmp, list(w = matrix(1:6 + 0.5, nrow = 2)))
ctx <- gguf_open(tmp)
gguf_tensors(ctx)
#>   name type n_dims dims n_elements nbytes offset
#> 1    w  f32      2 2, 3          6     24     96
unlink(tmp)
```
