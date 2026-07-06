# NEWS

## 0.1.0 (unreleased)

- Fixed native `Ops` edge semantics to match base R for logical `&`/`|` with `NA` and numeric `/` by zero (`Inf`/`NaN` instead of forcing `NA`), and corrected mixed-type native `Ops` regression tests.
- Optimized `[[` on fmalloc ALTREP vectors to bypass subset-copy for scalar extraction, removing a major per-element regression hotspot observed in scalar read loops.
- Added version 2 C API exports for zero-copy native interoperability: `Rfmalloc_is_fmalloc_vector`, `Rfmalloc_vector_type`, `Rfmalloc_vector_length`, and `Rfmalloc_vector_payload_ptr`.
- Added ALTREP regression tests ensuring scalar `[[` no longer returns wrapped fmalloc vectors and that out-of-bounds `[[` signals the expected bounds error.
- Added ALTREP attribute regression coverage for matrix/array/data.frame attribute roundtripping and set minimum R dependency to `R (>= 4.4.0)`.
- Added `create_fmalloc_matrix()` and `create_fmalloc_array()` constructors and `create_fmalloc_data_frame()` plus `as_fmalloc_matrix()`, `as_fmalloc_array()`, and `as_fmalloc_data_frame()` convenience converters for metadata-only reshaping.
- Switched `create_fmalloc_vector()` and matrix/array constructors to length/dimension validation that supports non-negative exact double lengths and long-vector-friendly `R_xlen_t` handling (up to `2^52`) while keeping dimension elements within `integer` limits.
- Added `copy = FALSE` mode to `as_fmalloc_matrix()` and `as_fmalloc_array()` to install shape metadata in-place on fmalloc ALTREP vectors via C-level attribute assignment, with new tests proving this mode avoids additional payload allocations.
- Added S3 class tagging for fmalloc vectors/matrices/arrays and method dispatch for core `Ops`, `Summary`, and matrix reduction families (`rowSums`, `colSums`, `rowMeans`, `colMeans`) so key operators and reductions execute on fmalloc-backed inputs through package-backed handlers.
- Replaced matrix summary/reduction fallback paths with explicit in-package kernels for `range`, `rowSums`, `colSums`, `rowMeans`, and `colMeans` so results match base R edge-case semantics while avoiding unnecessary large intermediate allocations.
- Clarified explicit fallback behavior: `rowSums()`, `colSums()`, `rowMeans()`, and `colMeans()` now warn and delegate to base R when inputs are not exact 2D matrices or `dims != 1L`; `Summary`/`Math`/`Math2` scalar or zero-length results remain ordinary R scalars by design.
- Added a runtime-sharing policy for opened runtimes in-process: opening the same
  backing file path now returns the existing shared runtime (with matching mode) and
  prevents accidental same-file multi-handle mode mismatches.
- Added `diagnose_fmalloc_runtime()` for runtime+catalog diagnostics, including record state counts, payload usage summaries, and an explicit compaction status note to explain why catalog compaction is not yet implemented.
- Implemented nested fmalloc list persistence by reference for persistent runtimes.
- Added recursive fmalloc list/container serialized-reference recovery in ALTREP
  serialization/unserialization.
- Added explicit vector destroy API `destroy_fmalloc_vector()` with mode-aware
  semantics and persistent unsafe reclaim mode.
- Added explicit reference-count failure behavior when destroying a vector that is
  still referenced by another fmalloc list.
- Added initial public R and C-callable API surface for runtime/vector
  introspection: `fmalloc_api_version()`, `fmalloc_default_runtime()`,
  `is_fmalloc_runtime()`, `is_fmalloc_vector()`, `fmalloc_runtime()`,
  `fmalloc_runtime_info()`, `fmalloc_vector_info()`, `fmalloc_vector_type()`,
  `fmalloc_vector_length()`, and `fmalloc_vector_payload_ptr()`, with runtime
  default synchronization for C-callable access.
- Added native C kernel implementations for fmalloc-backed linear algebra
  (`%*%`, `crossprod()`, and `tcrossprod()`), returning managed fmalloc matrix
  outputs with base-consistent shape behavior and name propagation.
- Fixed list child validation to require the same runtime pointer (not only matching UUID) when storing fmalloc elements in fmalloc list containers.
- Hardened ALTREP subset behavior by only taking the fast native path for strictly
  positive integer indexes; all other index types/values (including `0`, negative,
  fractional `REALSXP`, and mixed modes) now fall back to base R semantics.
- Switched serialized persistent metadata fields (`offset`, `nbytes`, `catalog_offset`,
  `generation`) to fixed-width hex string encoding for exact 64-bit persistence; restoration
  now accepts both new hex and legacy numeric encodings.
- Clarified that full view-based subset support remains experimental (current
  behavior is subset-copy with copy-on-write duplication controls).
- Documented Simon Urbanek custom allocator PoC separately from Rfmalloc's prior
  allocator implementation in README references.
