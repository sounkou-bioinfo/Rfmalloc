# Rggml 0.1.0 (unreleased)

- Initial release. Rggml is a carrier package: it vendors a CPU-only,
  architecture-generic build of the 'GGML' tensor library as a static
  library (`inst/ggml/libggml.a`), installs its headers, and exposes GGML's
  tensor-context/matrix-multiply compute path through `R_RegisterCCallable`
  C-callable entry points, declared for downstream use in
  `inst/include/Rggml.h`.
- Registered C-callables: context lifecycle (`Rggml_context_create`,
  `Rggml_context_free`, plus `Rggml_used_mem`/`Rggml_tensor_overhead`/
  `Rggml_graph_overhead` for sizing), tensor creation with an optional
  zero-copy external data pointer (`Rggml_new_tensor`, `Rggml_tensor_data`,
  `Rggml_tensor_set_data`, `Rggml_tensor_ne`, `Rggml_tensor_nb`), the CPU
  backend (`Rggml_backend_cpu_init`, `Rggml_backend_free`,
  `Rggml_backend_graph_compute`), graph building (`Rggml_new_graph`,
  `Rggml_build_forward_expand`), matrix multiply (`Rggml_mul_mat`, and the
  canned single-op `Rggml_compute_mul_mat` that also copies the F32 result
  into a caller-provided `float*`/`double*` buffer), and type/size
  introspection (`Rggml_type_size`, `Rggml_row_size`, `Rggml_blck_size`,
  `Rggml_nbytes`, `Rggml_nelements`, `Rggml_type_name`).
- Added `ggml_version()`, returning the vendored GGML library's own runtime
  version string.
- Verified and documented the `ggml_mul_mat()` convention: loading two R
  matrices directly into GGML tensors with `ne = dim(matrix)` (the raw
  column-major bytes, no copy or transpose), `ggml_mul_mat(ctx, A, B)`
  produces a result tensor of dim `(ncol(A), ncol(B))` equal to
  `crossprod(A, B)` (`t(A) %*% B`). Covered by the tinytest suite with both
  hand-checkable small cases and larger random matrices, through both the
  ggml-managed and the zero-copy (externally-owned buffer) tensor creation
  paths, exercised via the registered C-callables (not the internal
  implementation functions directly).
- CPU-only: no SIMD compiler flags, no OpenMP, no Vulkan/CUDA/Metal/etc
  backends, no hand-written x86 SIMD kernel files - GGML's portable
  architecture-generic reference kernels only, for CRAN-facing build
  portability. See README.md for what a future Vulkan backend would need.
