# (internal) Run a quantized (Q4_K) 'GGML' matmul through the C-callables

Not exported; the quantized analogue of
[`rggml_test_mul_mat()`](https://sounkou-bioinfo.github.io/Rfmalloc/Rggml/reference/rggml_test_mul_mat.md)
and the exact operation the Rfmalloc typed-GEMM bridge performs. The
weight matrix `A` is quantized to `Q4_K` into a heap buffer standing in
for an mmap'd 'GGUF' payload, wrapped zero-copy as a `Q4_K` tensor, and
multiplied by the dense F32 activations `B`. `ggml_mul_mat()` contracts
each `Q4_K` weight row against `B`'s columns through the
runtime-SIMD-dispatched `ggml_vec_dot_q4_K_q8_K` (AVX2/NEON where
staged), quantizing `B` to `Q8_K` on the fly as at inference.

## Usage

``` r
rggml_test_mul_mat_q4k(A, B)
```

## Arguments

- A:

  Numeric weight matrix; `nrow(A)` (the contracted dimension) must be a
  multiple of 256 (`QK_K`).

- B:

  Numeric activation matrix with `nrow(B) == nrow(A)`.

## Value

A numeric matrix, dim `c(ncol(A), ncol(B))`, equal to `crossprod(A, B)`
up to q4_K/q8_K quantization error.
