# Enable or disable the ggml quantized matmul backend

Rllm registers Rggml as an Rfmalloc codec-aware matrix-multiply backend
named `"ggml"` and selects it on load. When active, products of
quantized `fmalloc_tensor`s (types `"q4_0"`, `"q4_1"`, `"q8_0"`,
`"q2_k"`, `"q4_k"`, `"q6_k"`) where the tensor is the right-hand operand
(`dense %*% tensor`) are computed by ggml in quantized space,
contracting each weight row through GGML's SIMD-dispatched dot kernel,
with the dense operand quantized on the fly. Other products (the tensor
on the left, non-quantized codecs) are declined and fall back to
Rfmalloc's decode-then-BLAS path, so results are always correct
regardless of the selected backend.

## Usage

``` r
rllm_use_ggml(enable = TRUE)

rllm_backend_enabled()
```

## Arguments

- enable:

  If `TRUE` (default) select the `"ggml"` backend; if `FALSE` restore
  Rfmalloc's default BLAS path.

## Value

Invisibly, `TRUE` if the ggml backend is active afterwards.

`rllm_backend_enabled()` returns `TRUE` if the ggml backend is the
active Rfmalloc matmul backend.

## Details

Selection is Rfmalloc-scoped; base R's `%*%` is unaffected.

## See also

[`rllm_quantize_tensor()`](https://sounkou-bioinfo.github.io/Rfmalloc/Rllm/reference/rllm_quantize_tensor.md)

## Examples

``` r
rllm_backend_enabled()
#> [1] TRUE
rllm_use_ggml(FALSE)   # fall back to Rfmalloc's BLAS decode path
rllm_use_ggml(TRUE)    # re-enable ggml quantized products
```
