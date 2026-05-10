# NEWS

## 0.1.0 (unreleased)

- Added ALTREP attribute regression coverage for matrix/array/data.frame attribute roundtripping and set minimum R dependency to `R (>= 4.4.0)`.
- Added `create_fmalloc_matrix()` and `create_fmalloc_array()` constructors and `create_fmalloc_data_frame()` plus `as_fmalloc_matrix()`, `as_fmalloc_array()`, and `as_fmalloc_data_frame()` convenience converters for metadata-only reshaping.
- Added `copy = FALSE` mode to `as_fmalloc_matrix()` and `as_fmalloc_array()` to install shape metadata in-place on fmalloc ALTREP vectors via C-level attribute assignment, with new tests proving this mode avoids additional payload allocations.
- Added S3 class tagging for fmalloc vectors/matrices/arrays and method dispatch for core `Ops`, `Summary`, and matrix reduction families (`rowSums`, `colSums`, `rowMeans`, `colMeans`) so key operators and reductions execute on fmalloc-backed inputs through package-backed handlers.
- Replaced matrix summary/reduction fallback paths with explicit in-package kernels for `range`, `rowSums`, `colSums`, `rowMeans`, and `colMeans` so results match base R edge-case semantics while avoiding unnecessary large intermediate allocations.
- Clarified explicit fallback behavior: `rowSums()`, `colSums()`, `rowMeans()`, and `colMeans()` now warn and delegate to base R when inputs are not exact 2D matrices or `dims != 1L`; `Summary`/`Math`/`Math2` scalar or zero-length results remain ordinary R scalars by design.
- Added `diagnose_fmalloc_runtime()` for runtime+catalog diagnostics, including record state counts, payload usage summaries, and an explicit compaction status note to explain why catalog compaction is not yet implemented.
- Implemented nested fmalloc list persistence by reference for persistent runtimes.
- Added recursive fmalloc list/container serialized-reference recovery in ALTREP
  serialization/unserialization.
- Added explicit vector destroy API `destroy_fmalloc_vector()` with mode-aware
  semantics and persistent unsafe reclaim mode.
- Added explicit reference-count failure behavior when destroying a vector that is
  still referenced by another fmalloc list.
- Clarified that full view-based subset support remains experimental (current
  behavior is subset-copy with copy-on-write duplication controls).
- Documented Simon Urbanek custom allocator PoC separately from Rfmalloc's prior
  allocator implementation in README references.
