# What this Rggml was actually built with

Reports the build decisions `configure` made, read off the preprocessor
symbols compiled into the shared object. This is deliberately not a
re-detection of the host: it answers "what is in this binary", not "what
could this machine run".

## Usage

``` r
rggml_cpu_info()
```

## Value

A list with

- `arch_kernels`:

  `"arm"` for GGML's hand-tuned NEON kernels, `"wasm"` for its SIMD128
  kernels, and `"generic"` for the portable reference kernels.

- `simd_dispatch`:

  `TRUE` when the runtime CPUID dispatcher is active (x86: the staged
  AVX2 `q4_K` variant). Always `FALSE` alongside `arch_kernels = "arm"`
  or `"wasm"`, which supersede it.

- `blas`:

  `TRUE` when GGML's BLAS backend and R's Fortran bridge are part of
  this target build. It is `FALSE` on wasm, where webR's hidden
  character-length ABI is incompatible with the native bridge.

- `sgemm`:

  `TRUE` when R's BLAS exports `sgemm_` and GGML's BLAS backend calls it
  directly; `FALSE` when the shim promotes to `dgemm_` or when `blas` is
  `FALSE`.

- `vulkan`:

  `TRUE` when built with `--with-vulkan`. Whether a *device* is visible
  is a separate question: see
  [`rggml_vulkan_info()`](https://sounkou-bioinfo.github.io/Rfmalloc/Rggml/reference/rggml_vulkan_info.md).

- `cuda`:

  `TRUE` when built with `--with-cuda`. Whether a *device* is visible is
  a separate question: see
  [`rggml_cuda_info()`](https://sounkou-bioinfo.github.io/Rfmalloc/Rggml/reference/rggml_cuda_info.md).

## Details

It exists because the failure mode it guards against is invisible. A
build that misses its intended branch - falling back to GGML's portable
scalar kernels where the native NEON ones were meant, or to the `dgemm_`
promotion where `sgemm_` was available - compiles cleanly and passes
every numerical test, because both branches are correct. Only the speed
differs. Asserting on this list in the test suite turns those silent
fallbacks into failures.

## See also

[`rggml_vulkan_info()`](https://sounkou-bioinfo.github.io/Rfmalloc/Rggml/reference/rggml_vulkan_info.md),
[`rggml_cuda_info()`](https://sounkou-bioinfo.github.io/Rfmalloc/Rggml/reference/rggml_cuda_info.md)

## Examples

``` r
rggml_cpu_info()
#> $arch_kernels
#> [1] "generic"
#> 
#> $simd_dispatch
#> [1] TRUE
#> 
#> $blas
#> [1] TRUE
#> 
#> $sgemm
#> [1] TRUE
#> 
#> $vulkan
#> [1] FALSE
#> 
#> $cuda
#> [1] FALSE
#> 
```
