# Changelog

## Rggml 0.1.0 (unreleased)

- Added the **Vulkan GPU backend** (API version 7), vendored from the
  same pinned ggmlR tarball as the CPU core (so it version-matches it)
  through `tools/vendor-ggml`. It is **opt-in at build time**, never
  auto-detected:

      install.packages("Rggml", configure.args = "--with-vulkan")
      R CMD INSTALL --configure-args=--with-vulkan .

  Auto-detection would be hostile: GGML embeds 156 compiled SPIR-V
  shaders as C++ translation units, and the largest (`mul_mm`, a 141 MB
  array literal) needs ~5 GB of RAM to compile - independent of `-O`
  level, since the cost is parsing the literal. `configure` probes
  `glslc`’s optional shader features (coopmat, coopmat2, integer_dot,
  bfloat16), builds the shader generator, and compiles the shader set
  those features imply; it errors out clearly if `--with-vulkan` is
  given without `glslc`/Vulkan headers. Without the flag, nothing about
  the build changes. New C-callables: `Rggml_backend_vulkan_init`,
  `Rggml_backend_vulkan_device_count`, and
  `Rggml_backend_vulkan_device_description`; they report zero devices
  instead of failing when the backend was not built, so callers can
  probe and fall back. R-level:
  [`rggml_vulkan_info()`](https://sounkou-bioinfo.github.io/Rfmalloc/Rggml/reference/rggml_vulkan_info.md),
  [`rggml_has_vulkan()`](https://sounkou-bioinfo.github.io/Rfmalloc/Rggml/reference/rggml_vulkan_info.md).

- Added the **device-buffer residency C-callables** (API version 7):
  `Rggml_backend_alloc_ctx_tensors`, `Rggml_backend_buffer_free`,
  `Rggml_backend_tensor_set`, `Rggml_backend_tensor_get`. The CPU and
  BLAS backends compute on host memory, so pointing a tensor at an R
  buffer works; a GPU backend’s tensors must live in device memory.
  These wrap GGML’s backend-agnostic path - allocate a no_alloc
  context’s tensors into one backend buffer, upload, compute, download -
  which is identical for CPU, BLAS and Vulkan, and is what
  `test_vulkan.R` uses to assert the GPU backend computes the same
  product as the CPU one.

- Added **view and copy C-callables** (API version 6): `Rggml_view_1d`/
  `_2d`/`_3d` (byte offsets/strides, as in ggml) and `Rggml_cpy` - the
  building blocks of a KV cache: cpy nodes write new keys/values into
  views of a persistent cache tensor, expanded into the graph ahead of
  the attention nodes that read other views of the same cache (see
  Rllm’s incremental decoding).

- Added the transformer **graph-op C-callables** (API version 5): the
  ops a forward pass composes - `Rggml_get_rows`, `Rggml_rms_norm`,
  `Rggml_mul`, `Rggml_add`, `Rggml_silu`, `Rggml_scale`,
  `Rggml_soft_max`, `Rggml_diag_mask_inf`, `Rggml_rope` (wrapping
  `ggml_rope_ext` with YaRN off), and the shape ops
  `Rggml_reshape_2d`/`_3d`, `Rggml_permute`, `Rggml_cont`,
  `Rggml_transpose`. Downstream packages can now assemble and compute
  full transformer graphs (see Rllm’s llama forward pass) without
  linking GGML.

- Added quantization C-callables (API version 4): `Rggml_quantize` wraps
  `ggml_quantize_chunk()` so downstream packages can encode f32 rows
  into any GGUF block format (the output is byte-compatible with GGUF
  tensor payloads), and `Rggml_dequantize` wraps GGML’s type-traits
  `to_float` - the authoritative reference dequantizer, used by the
  Rfmalloc ecosystem to cross-validate its codec decoders (this
  cross-validation caught and pinned a Q4_K decode bug in Rgguf’s
  vendored gguflib; see that package’s NEWS).

- Verified the full quantized-weight compute path over an external
  payload: a `Q4_K` tensor pointed zero-copy at a heap buffer standing
  in for an mmap’d GGUF payload, multiplied by dense F32 activations via
  `ggml_mul_mat()` on the CPU backend, routes each weight row through
  the runtime-SIMD-dispatched `vec_dot` (activations quantized to `Q8_K`
  on the fly, as at llama.cpp inference) and tracks the true product to
  q4_K accuracy (`test_mul_mat_q4k.R`).

- Initial release. Rggml is a carrier package: it vendors a CPU-only,
  architecture-generic build of the ‘GGML’ tensor library as a static
  library (`inst/ggml/libggml.a`), installs its headers, and exposes
  GGML’s tensor-context/matrix-multiply compute path through
  `R_RegisterCCallable` C-callable entry points, declared for downstream
  use in `inst/include/Rggml.h`.

- Registered C-callables: context lifecycle (`Rggml_context_create`,
  `Rggml_context_free`, plus `Rggml_used_mem`/`Rggml_tensor_overhead`/
  `Rggml_graph_overhead` for sizing), tensor creation with an optional
  zero-copy external data pointer (`Rggml_new_tensor`,
  `Rggml_tensor_data`, `Rggml_tensor_set_data`, `Rggml_tensor_ne`,
  `Rggml_tensor_nb`), the CPU backend (`Rggml_backend_cpu_init`,
  `Rggml_backend_free`, `Rggml_backend_graph_compute`), graph building
  (`Rggml_new_graph`, `Rggml_build_forward_expand`), matrix multiply
  (`Rggml_mul_mat`, and the canned single-op `Rggml_compute_mul_mat`
  that also copies the F32 result into a caller-provided
  `float*`/`double*` buffer), and type/size introspection
  (`Rggml_type_size`, `Rggml_row_size`, `Rggml_blck_size`,
  `Rggml_nbytes`, `Rggml_nelements`, `Rggml_type_name`).

- Added **runtime SIMD dispatch** for GGML’s hot quantized vec-dot
  kernels, starting with `q4_K x q8_K`. `configure` compiles the kernel
  into ISA-specific variants (`-mavx2 -mfma -O3` on x86 incl. Intel
  macOS; `-O3` NEON on aarch64 incl. Apple Silicon) staged under
  `tools/simd/`, and a CPUID dispatcher
  (`tools/simd/rggml_simd_dispatch.c`, using a vendored copy of
  RsimdDispatch’s `cpu_features`) selects the best at runtime, falling
  back to GGML’s scalar reference. The ISA flags live in `configure`,
  not in R’s recorded package flags, so R CMD check raises no
  non-portable-flags NOTE; a variant is only called after its ISA is
  confirmed at runtime (single `.so`, no `dlopen`, unlike GGML’s own
  `GGML_CPU_ALL_VARIANTS`). The AVX2 q4_K dot measures ~6-7x faster than
  the scalar reference on x86.

- Enabled GGML’s **BLAS backend** (`Rggml_backend_blas_init`,
  `Rggml_backend_blas_set_n_threads`): dense F32 `mul_mat` offloads to
  whatever BLAS the R build links against, since BLAS is universal in R.
  GGML’s backend calls the C `cblas_sgemm`, which R does not guarantee,
  so Rggml bridges it with a small portable shim (`inst/ggml/cblas.h` +
  `rggml_cblas.c`) forwarding to Fortran `sgemm_` via `F77_NAME()`,
  linking `$(BLAS_LIBS) $(FLIBS)`. The BLAS backend is a drop-in
  `backend` for the existing
  `Rggml_compute_mul_mat`/`Rggml_backend_graph_compute` path.

- Added
  [`ggml_version()`](https://sounkou-bioinfo.github.io/Rfmalloc/Rggml/reference/ggml_version.md),
  returning the vendored GGML library’s own runtime version string.

- Verified and documented the `ggml_mul_mat()` convention: loading two R
  matrices directly into GGML tensors with `ne = dim(matrix)` (the raw
  column-major bytes, no copy or transpose), `ggml_mul_mat(ctx, A, B)`
  produces a result tensor of dim `(ncol(A), ncol(B))` equal to
  `crossprod(A, B)` (`t(A) %*% B`). Covered by the tinytest suite with
  both hand-checkable small cases and larger random matrices, through
  both the ggml-managed and the zero-copy (externally-owned buffer)
  tensor creation paths, exercised via the registered C-callables (not
  the internal implementation functions directly).

- CPU kernels are architecture-generic (no `-march` SIMD flags, no
  OpenMP, no hand-written x86 SIMD kernel files) for CRAN-facing build
  portability, plus the BLAS backend above for dense products. No
  Vulkan/CUDA/Metal GPU backends. Runtime-SIMD-dispatched quantized
  kernels (`GGML_CPU_ALL_VARIANTS`) and a Vulkan backend are planned;
  see README.md.
