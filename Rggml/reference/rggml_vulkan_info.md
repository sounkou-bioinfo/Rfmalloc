# Vulkan GPU backend availability

Reports the Vulkan devices GGML can see. Rggml only contains the Vulkan
backend when it was **built with it** - it is opt-in, because generating
and compiling GGML's 156 embedded SPIR-V shaders is expensive (the
largest one needs several GB of RAM):

## Usage

``` r
rggml_vulkan_info()

rggml_has_vulkan()
```

## Value

A list with `n_devices` (integer) and `device` (the description of
device 0, or `NA` when there is none).

`rggml_has_vulkan()` returns `TRUE` when at least one Vulkan device is
usable.

## Details


      install.packages("Rggml", configure.args = "--with-vulkan")
      R CMD INSTALL --configure-args=--with-vulkan .

It also requires `glslc` and the Vulkan headers at build time
(`libvulkan-dev` + `glslc` on Debian/Ubuntu, or the LunarG Vulkan SDK
with `VULKAN_SDK` set), and a Vulkan driver at run time. A software
driver such as Mesa's lavapipe counts: it is slow, but it makes the
backend testable without a GPU.

When Rggml was built without Vulkan, this returns zero devices rather
than failing, so callers can probe and fall back.

Two opt-in environment variables widen which devices are usable, both
off by default (so the default is upstream GGML's):
`GGML_VK_ALLOW_CPU=1` accepts a CPU-type Vulkan device (e.g. Mesa
lavapipe), and `GGML_VK_ALLOW_128_PUSH=1` accepts a device that exposes
only 128-byte push constants (e.g. a GPU reached through the Mesa dzn
D3D12 translation layer under WSL) for matrix multiply and other `<=4D`
operations; 5-D non-contiguous copies still require a 256-byte device.

## Examples

``` r
rggml_vulkan_info()
#> $n_devices
#> [1] 0
#> 
#> $device
#> [1] NA
#> 
```
