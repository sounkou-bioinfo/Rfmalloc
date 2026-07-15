# Vendored GGML: provenance and regeneration

`packages/Rggml/inst/ggml` and the GGML headers under
`packages/Rggml/inst/include` are generated. They are never edited in place.
The engine has one upstream source:

```
selected ggml-org/ggml v0.16.0 files
+ patches/ and header-patches/
+ overlay/
```

The selected source comes from official tag `v0.16.0`, commit
`524f974bb21a1013408f76d71c15732482c0c3fe`. The recipe hashes the extracted
paths and contents instead of trusting GitHub's archive compression. Its 479
selected files have tree sha256
`81dcabafee82839a6ce3cefd61e218db7411af7a8f5abbaeaa6fd02bad9cd266`.

Run `Rscript tools/vendor-ggml/vendorggml.R vendor` to regenerate the committed
tree. Run `Rscript tools/vendor-ggml/vendorggml.R check` to derive it in a
temporary directory and compare both file sets and every file hash. The check
also rejects stale source files left by an earlier recipe.

## Selected upstream surface

The manifest carries GGML's core tensor and graph engine, allocator, backend
registry, official GGUF reader and writer, quantization code, portable CPU
backend, BLAS backend, aarch64 and wasm quantized kernels, Vulkan backend and
shader sources, and CUDA backend. Public headers for those components come
from the same source tree.

Rggml deliberately does not carry training, RPC, Metal, SYCL, CANN, OpenCL,
AMX implementation files, or upstream's compile-time x86 variants. Dense F32
products use GGML's BLAS backend through R's BLAS on native targets. x86
quantized dispatch is owned by Rggml's runtime dispatcher, aarch64 uses
upstream's mandatory NEON baseline, and wasm uses upstream's SIMD128 kernels.

`gguf.cpp` and `gguf.h` are not a second import. They are part of this same
pin. Rgguf calls the narrow GGUF C-callables exported by Rggml, so parsing,
writing, tensor types and quantized decoding all refer to one GGML engine.

## Local patches

The source patches preserve a small set of integration fixes. Backend buffer
interfaces are passed by pointer because the large by-value C ABI failed under
Windows MinGW. Meta-backend objects which retain driver state live until
process exit, avoiding cross-library static destruction order. Context and
graph allocation paths return `NULL` when their memory pool is exhausted.
The CPU fallback header leaves the canonical q4_K dot symbol to Rggml's runtime
dispatcher and supplies the aliases needed beside the official aarch64
kernels.

The Vulkan patch permits a CPU-type Vulkan device only when
`GGML_VK_ALLOW_CPU=1`, which lets CI exercise the real backend through Mesa
lavapipe. The CUDA and Vulkan patches also follow the internal by-pointer
buffer interface. The public `ggml.h` patch lets the R compatibility header
disable format attributes before its `printf` redirection is visible.

No generated file is a patch source. Change a patch or overlay, then rerun the
vendor recipe.

## R integration overlay

The overlay contains the static-library Makefile, the CBLAS to R Fortran BLAS
bridge, and the R-safe diagnostic shim. CUDA and Vulkan are empty by default
and compile only when configure receives `--with-cuda` or `--with-vulkan`.
Architecture switches stay inside the static-library build and never enter R's
recorded package flags.

The diagnostic shim is adapted from ggmlR and remains under its MIT license.
It is the only retained ggmlR-derived component. The GGML engine itself,
including BLAS, GGUF, CUDA and Vulkan, now comes directly from ggml-org. The
BLAS bridge and build integration are Rggml code under GPL (>= 2).

## Backend contract

CPU is always present. BLAS is present on native R targets and omitted on
webR, whose flang ABI adds hidden character-length arguments that do not match
the native C-to-Fortran bridge. Vulkan and CUDA are optional implementations of
the same backend-buffer contract. A no-allocation tensor context is assigned to
a backend buffer, inputs are uploaded with `ggml_backend_tensor_set`, the graph
runs, and outputs are downloaded with `ggml_backend_tensor_get`. A build
without a backend reports it unavailable so callers can probe and fall back.

CUDA accepts `CUDA_HOME` or `--with-cuda=/path` and an optional
`RGGML_CUDA_ARCH`. Its real-device path is validated on the NVIDIA rig. Vulkan
correctness remains testable with Mesa lavapipe.
