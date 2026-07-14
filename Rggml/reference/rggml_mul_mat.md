# Matrix product on a chosen 'GGML' backend

Computes `crossprod(A, B)` (that is `t(A) %*% B`) through 'GGML' on the
requested backend, using the backend-agnostic residency path: the
operands and result are allocated in one of the backend's own buffers,
the inputs uploaded, the product computed, and the result downloaded.
For a GPU backend the tensors live in device memory, so this is the
entry point a caller uses to run a dense single-precision GEMM on the
GPU.

## Usage

``` r
rggml_mul_mat(A, B, backend = c("cpu", "blas", "vulkan", "cuda"))
```

## Arguments

- A, B:

  Numeric matrices with the same number of rows (the contracted
  dimension). Computed in single precision on every backend.

- backend:

  One of `"cpu"` (`ggml_backend_cpu_init()`), `"blas"` (offloads the
  dense F32 product to whatever BLAS the R build links against), or
  `"vulkan"` (a Vulkan device; requires Rggml built with `--with-vulkan`
  and a visible device, see
  [`rggml_vulkan_info()`](https://sounkou-bioinfo.github.io/Rfmalloc/Rggml/reference/rggml_vulkan_info.md)),
  or `"cuda"` (an NVIDIA CUDA device; requires Rggml built with
  `--with-cuda`, see
  [`rggml_cuda_info()`](https://sounkou-bioinfo.github.io/Rfmalloc/Rggml/reference/rggml_cuda_info.md)).
  Errors if the requested backend is unavailable.

## Value

A numeric matrix, dim `c(ncol(A), ncol(B))`, equal to `crossprod(A, B)`
up to single-precision rounding.

## See also

[`rggml_vulkan_info()`](https://sounkou-bioinfo.github.io/Rfmalloc/Rggml/reference/rggml_vulkan_info.md),
[`rggml_cuda_info()`](https://sounkou-bioinfo.github.io/Rfmalloc/Rggml/reference/rggml_cuda_info.md),
[`rggml_cpu_info()`](https://sounkou-bioinfo.github.io/Rfmalloc/Rggml/reference/rggml_cpu_info.md)

## Examples

``` r
A <- matrix(rnorm(12), 4, 3)
B <- matrix(rnorm(8), 4, 2)
rggml_mul_mat(A, B)               # == crossprod(A, B) to fp32
#>           [,1]       [,2]
#> [1,] -4.546084 -0.6009226
#> [2,] -1.062304 -1.1480335
#> [3,] -1.498720 -0.7332473
```
