# Rggml: Vendored 'GGML' Tensor Library with C-Callable Compute API

Rggml is a carrier package: it vendors the CPU backend of the 'GGML'
tensor library (<https://github.com/ggml-org/ggml>) as a static library,
installs its headers, and exposes 'GGML' tensor-context and
matrix-multiply compute through `R_RegisterCCallable()` entry points. It
has no high-level R modeling API of its own beyond
[`ggml_version`](https://sounkou-bioinfo.github.io/Rfmalloc/Rggml/reference/ggml_version.md).

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

## CPU only

Only the CPU backend is built, using GGML's architecture-generic
(non-SIMD) reference kernels for maximum portability across CRAN build
machines. See `README.md` for what a future 'Vulkan' backend would
require.

## See also

Useful links:

- <https://github.com/sounkou-bioinfo/Rggml>

- Report bugs at <https://github.com/sounkou-bioinfo/Rggml/issues>

## Author

**Maintainer**: Sounkou Mahamane Toure <sounkoutoure@gmail.com>

Other contributors:

- Georgi Gerganov (Author of the GGML library) \[copyright holder\]

- The ggml.ai / llama.cpp contributors (GGML CPU backend contributors)
  \[copyright holder\]

- Yuri Baramykov (ggmlR: source of the vendored GGML copy and its
  R/CRAN-compliance I/O shim, adapted here) \[contributor\]
