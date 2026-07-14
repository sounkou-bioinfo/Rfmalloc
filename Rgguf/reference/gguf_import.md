# Import GGUF Tensors

Reads some or all tensors from a GGUF file into a named list of
Rfmalloc-backed matrices/arrays, dequantizing as needed. This is a thin
convenience wrapper around repeated
[`gguf_tensor()`](https://sounkou-bioinfo.github.io/Rfmalloc/Rgguf/reference/gguf_tensor.md)
calls that shares a single open file handle across all of them.

## Usage

``` r
gguf_import(
  path_or_ctx,
  tensors = NULL,
  runtime = NULL,
  as = c("numeric", "native", "view")
)
```

## Arguments

- path_or_ctx:

  Either a path to a GGUF file, or a `"gguf_ctx"` object returned by
  [`gguf_open()`](https://sounkou-bioinfo.github.io/Rfmalloc/Rgguf/reference/gguf_open.md).

- tensors:

  Optional character vector of tensor names to import. If `NULL` (the
  default), every tensor in the file is imported.

- runtime:

  Optional `Rfmalloc` runtime handle passed through to every
  [`gguf_tensor()`](https://sounkou-bioinfo.github.io/Rfmalloc/Rgguf/reference/gguf_tensor.md)
  call, so all imported tensors share the same backing file. If `NULL`,
  `Rfmalloc`'s own default-runtime resolution is used for each tensor.

- as:

  Passed through to
  [`gguf_tensor()`](https://sounkou-bioinfo.github.io/Rfmalloc/Rgguf/reference/gguf_tensor.md):
  `"numeric"` dequantizes to fmalloc double matrices/arrays, `"native"`
  copies the encoded bytes into fmalloc storage, and `"view"` borrows
  the original read-only GGUF spans.

## Value

A named list of tensors, in the order requested (or file order, if
`tensors` is `NULL`). Numeric and native imports own fmalloc
allocations; views are read-only spans over the original GGUF mapping.

## Examples

``` r
tmp <- tempfile(fileext = ".gguf")
gguf_write_tensors(tmp, list(
    w1 = matrix(as.double(1:12), nrow = 4, ncol = 3),
    w2 = matrix(as.double(1:6), nrow = 3, ncol = 2)
))
rt <- Rfmalloc::open_fmalloc(tempfile(fileext = ".bin"))
mats <- gguf_import(tmp, runtime = rt)
prod <- mats$w1 %*% mats$w2
Rfmalloc::is_fmalloc_vector(prod)
#> [1] TRUE
unlink(tmp)
```
