# Rggml: Vendored 'GGML' Tensor Library with C-Callable Compute API

Rggml is a low-level carrier package for the 'GGML' tensor library
(<https://github.com/ggml-org/ggml>). Its generated static library
contains the core, official 'GGUF' implementation, CPU backend, the
native 'BLAS' bridge, and the opt-in 'Vulkan' and 'CUDA' backends.
Sibling packages consume these through `R_RegisterCCallable()` rather
than re-vendoring 'GGML'. Model composition belongs in Rllm and the
R-facing 'GGUF' storage layer belongs in Rgguf.

## For downstream package authors

Add `Rggml` to `LinkingTo` (and `Imports`, so the namespace is
guaranteed loaded first) in your package's `DESCRIPTION`, then
`#include <Rggml.h>` in your C/C++ source. That header also pulls in the
vendored public 'GGML' headers (`ggml.h`, `ggml-alloc.h`,
`ggml-backend.h`, `ggml-cpu.h`), so real 'GGML' types
(`struct ggml_context *`, `struct ggml_tensor *`, `ggml_backend_t`, ...)
are available directly - no re-vendoring of 'GGML' and no linking
against Rggml's shared object is required. Every wrapped function has an
`<Name>_ptr()` accessor that resolves the symbol via `R_GetCCallable()`
the first time it is needed.

## Compute backends

The CPU backend is always built. The 'BLAS' backend is present on native
R targets and omitted on webR, whose Fortran character ABI differs. On
x86, selected quantized kernels are staged with ISA flags by `configure`
and selected by runtime dispatch. On aarch64, 'GGML' 'NEON' kernels are
the baseline. The wasm target uses 'GGML' 'SIMD128' quant kernels. The
'Vulkan' backend is opt-in at installation with `--with-vulkan`.

## See also

Useful links:

- <https://github.com/sounkou-bioinfo/Rfmalloc>

- <https://sounkou-bioinfo.github.io/Rfmalloc/Rggml/>

- Report bugs at <https://github.com/sounkou-bioinfo/Rfmalloc/issues>

## Author

**Maintainer**: Sounkou Mahamane Toure <sounkoutoure@gmail.com>

Authors:

- Sounkou Mahamane Toure <sounkoutoure@gmail.com>

Other contributors:

- Georgi Gerganov (Author of the GGML library) \[copyright holder\]

- The ggml.ai / llama.cpp contributors (GGML CPU backend contributors)
  \[copyright holder\]

- Yuri Baramykov (Author of the ggmlR R/CRAN-compliance I/O shim adapted
  here) \[contributor\]
