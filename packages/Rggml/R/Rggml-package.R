#' Rggml: Vendored 'GGML' Tensor Library with C-Callable Compute API
#'
#' Rggml is a low-level carrier package for the 'GGML' tensor library
#' (\url{https://github.com/ggml-org/ggml}). Its generated static library
#' contains the core, official 'GGUF' implementation, CPU backend, the native
#' 'BLAS' bridge, and the opt-in 'Vulkan' and 'CUDA' backends. Sibling packages
#' consume these through \code{R_RegisterCCallable()} rather than re-vendoring
#' 'GGML'. Model composition belongs in Rllm and the R-facing 'GGUF' storage
#' layer belongs in Rgguf.
#'
#' @section For downstream package authors:
#' Add \code{Rggml} to \code{LinkingTo} (and \code{Imports}, so the
#' namespace is guaranteed loaded first) in your package's \code{DESCRIPTION},
#' then \code{#include <Rggml.h>} in your C/C++ source. That header also
#' pulls in the vendored public 'GGML' headers (\code{ggml.h},
#' \code{ggml-alloc.h}, \code{ggml-backend.h}, \code{ggml-cpu.h}), so real
#' 'GGML' types (\code{struct ggml_context *}, \code{struct ggml_tensor *},
#' \code{ggml_backend_t}, ...) are available directly - no re-vendoring of
#' 'GGML' and no linking against Rggml's shared object is required. Every
#' wrapped function has an \verb{<Name>_ptr()} accessor that resolves the
#' symbol via \code{R_GetCCallable()} the first time it is needed.
#'
#' @section Compute backends:
#' The CPU backend is always built. The 'BLAS' backend is present on native R
#' targets and omitted on webR, whose Fortran character ABI differs. On x86,
#' selected quantized kernels are staged with ISA flags by \code{configure} and
#' selected by runtime dispatch. On aarch64, 'GGML' 'NEON' kernels are the
#' baseline. The wasm target uses 'GGML' 'SIMD128' quant kernels. The 'Vulkan'
#' backend is opt-in at installation with \code{--with-vulkan}.
#'
#' @keywords internal
#' @useDynLib Rggml, .registration = TRUE
"_PACKAGE"
