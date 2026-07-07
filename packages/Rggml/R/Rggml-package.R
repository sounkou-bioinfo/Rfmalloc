#' Rggml: Vendored 'GGML' Tensor Library with C-Callable Compute API
#'
#' Rggml is a carrier package: it vendors the CPU backend of the 'GGML'
#' tensor library (\url{https://github.com/ggml-org/ggml}) as a static
#' library, installs its headers, and exposes 'GGML' tensor-context and
#' matrix-multiply compute through \code{R_RegisterCCallable()} entry
#' points. It has no high-level R modeling API of its own beyond
#' \code{\link{ggml_version}}.
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
#' @section CPU only:
#' Only the CPU backend is built, using GGML's architecture-generic
#' (non-SIMD) reference kernels for maximum portability across CRAN build
#' machines. See \code{README.md} for what a future 'Vulkan' backend would
#' require.
#'
#' @keywords internal
#' @useDynLib Rggml, .registration = TRUE
"_PACKAGE"
