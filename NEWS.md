# NEWS

## Development (unreleased)

- Added ALTREP attribute regression coverage for matrix/array/data.frame attribute roundtripping and set minimum R dependency to `R (>= 4.4.0)`.
- Added explicit constructor helpers for structured fmalloc objects:
  - `create_fmalloc_matrix()` and `create_fmalloc_array()` for metadata-initialized ALTREP matrix/array allocation.
  - `create_fmalloc_data_frame()` plus `as_fmalloc_matrix()`, `as_fmalloc_array()`, and `as_fmalloc_data_frame()` convenience converters for metadata-only reshaping.
- Added `copy = FALSE` mode to `as_fmalloc_matrix()` and `as_fmalloc_array()` to install shape metadata in-place on fmalloc ALTREP vectors via C-level attribute assignment, with new tests proving this mode avoids additional payload allocations.
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
