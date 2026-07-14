# CUDA GPU backend availability

Reports the NVIDIA CUDA devices GGML can see. The backend is opt-in at
installation because it requires the CUDA toolkit and compiles native
GPU kernels:

## Usage

``` r
rggml_cuda_info()

rggml_has_cuda()
```

## Value

A list with `n_devices` (integer) and `device` (the description of
device 0, or `NA` when there is none).

`rggml_has_cuda()` returns `TRUE` when at least one CUDA device is
usable.

## Details


      install.packages("Rggml", configure.args = "--with-cuda")
      R CMD INSTALL --configure-args=--with-cuda .

Set `CUDA_HOME` when the toolkit is outside the search path. By default
the source installation targets the GPU visible at build time. Set
`RGGML_CUDA_ARCH` to an explicit nvcc architecture such as `sm_89` or
`sm_120a` when building for another machine.

A build without CUDA returns zero devices rather than failing, so
callers can probe and fall back to Vulkan, BLAS or CPU computation.

## Examples

``` r
rggml_cuda_info()
#> $n_devices
#> [1] 0
#> 
#> $device
#> [1] NA
#> 
```
