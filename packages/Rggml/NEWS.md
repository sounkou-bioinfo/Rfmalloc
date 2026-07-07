# Rggml 0.1.0 (unreleased)

- Added quantization C-callables (API version 4): `Rggml_quantize` wraps
  `ggml_quantize_chunk()` so downstream packages can encode f32 rows into any
  GGUF block format (the output is byte-compatible with GGUF tensor payloads),
  and `Rggml_dequantize` wraps GGML's type-traits `to_float` - the
  authoritative reference dequantizer, used by the Rfmalloc ecosystem to
  cross-validate its codec decoders (this cross-validation caught and pinned a
  Q4_K decode bug in Rgguf's vendored gguflib; see that package's NEWS).
- Verified the full quantized-weight compute path over an external payload:
  a `Q4_K` tensor pointed zero-copy at a heap buffer standing in for an
  mmap'd GGUF payload, multiplied by dense F32 activations via
  `ggml_mul_mat()` on the CPU backend, routes each weight row through the
  runtime-SIMD-dispatched `vec_dot` (activations quantized to `Q8_K` on the
  fly, as at llama.cpp inference) and tracks the true product to q4_K
  accuracy (`test_mul_mat_q4k.R`).
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
- Added **runtime SIMD dispatch** for GGML's hot quantized vec-dot kernels,
  starting with `q4_K x q8_K`. `configure` compiles the kernel into
  ISA-specific variants (`-mavx2 -mfma -O3` on x86 incl. Intel macOS; `-O3`
  NEON on aarch64 incl. Apple Silicon) staged under `tools/simd/`, and a CPUID
  dispatcher (`tools/simd/rggml_simd_dispatch.c`, using a vendored copy of
  RsimdDispatch's `cpu_features`) selects the best at runtime, falling back to
  GGML's scalar reference. The ISA flags live in `configure`, not in R's
  recorded package flags, so R CMD check raises no non-portable-flags NOTE; a
  variant is only called after its ISA is confirmed at runtime (single `.so`,
  no `dlopen`, unlike GGML's own `GGML_CPU_ALL_VARIANTS`). The AVX2 q4_K dot
  measures ~6-7x faster than the scalar reference on x86.
- Enabled GGML's **BLAS backend** (`Rggml_backend_blas_init`,
  `Rggml_backend_blas_set_n_threads`): dense F32 `mul_mat` offloads to
  whatever BLAS the R build links against, since BLAS is universal in R.
  GGML's backend calls the C `cblas_sgemm`, which R does not guarantee, so
  Rggml bridges it with a small portable shim (`inst/ggml/cblas.h` +
  `rggml_cblas.c`) forwarding to Fortran `sgemm_` via `F77_NAME()`, linking
  `$(BLAS_LIBS) $(FLIBS)`. The BLAS backend is a drop-in `backend` for the
  existing `Rggml_compute_mul_mat`/`Rggml_backend_graph_compute` path.
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
- CPU kernels are architecture-generic (no `-march` SIMD flags, no OpenMP,
  no hand-written x86 SIMD kernel files) for CRAN-facing build portability,
  plus the BLAS backend above for dense products. No Vulkan/CUDA/Metal GPU
  backends. Runtime-SIMD-dispatched quantized kernels
  (`GGML_CPU_ALL_VARIANTS`) and a Vulkan backend are planned; see README.md.
