# Pluggable matrix-multiply backend

Rfmalloc's matrix-product kernels (`%*%`,
[`crossprod()`](https://rdrr.io/r/base/crossprod.html),
[`tcrossprod()`](https://rdrr.io/r/base/crossprod.html), the out-of-core
and typed-tensor products) dispatch their `dgemm` calls through a
selectable backend. By default this is R's BLAS; downstream packages can
register an alternative (for example a GPU cuBLAS kernel or an
out-of-core-aware GEMM) through the `Rfmalloc_register_matmul_backend`
C-callable and select it here. Selection is Rfmalloc-scoped: base R's
`%*%` is unaffected.

## Usage

``` r
fmalloc_matmul_backend(name = NULL)

fmalloc_matmul_backends()
```

## Arguments

- name:

  Backend name to select. `"blas"` (or `NULL`/`""`) selects the default
  BLAS path.

## Value

`fmalloc_matmul_backend()` returns the active backend name;
`fmalloc_matmul_backends()` returns the registered backend names (BLAS
is always available and not listed).

## Details

A registered backend may decline a given call (returning non-zero), in
which case Rfmalloc falls back to R's BLAS for that product.

## Examples

``` r
fmalloc_matmul_backend()      # "blas" by default
#> [1] "blas"
fmalloc_matmul_backends()     # names registered by other packages
#> character(0)
```
