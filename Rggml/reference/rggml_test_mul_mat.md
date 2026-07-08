# (internal) Run a 'GGML' matmul through the registered C-callables

Not exported; exists so the tinytest suite can exercise the full
registered C-callable path (context/tensor creation, the CPU backend,
and `ggml_mul_mat()`) exactly as a downstream `LinkingTo` package would,
driven from R.

## Usage

``` r
rggml_test_mul_mat(A, B, zero_copy = FALSE, backend = c("cpu", "blas"))
```

## Arguments

- A, B:

  Numeric matrices with the same number of rows.

- zero_copy:

  Logical; if `TRUE`, the tensors wrap externally owned buffers (the
  mmap-style zero-copy path) instead of 'GGML'-managed ones.

- backend:

  Which registered backend computes the product: `"cpu"` (default) uses
  `ggml_backend_cpu_init()`; `"blas"` uses `ggml_backend_blas_init()`,
  which offloads the dense F32 product to whatever BLAS the R build
  links against.

## Value

A numeric matrix, dim `c(ncol(A), ncol(B))`, equal to `crossprod(A, B)`
(i.e. `t(A) %*% B`) computed via `ggml_mul_mat()` on the chosen backend.
