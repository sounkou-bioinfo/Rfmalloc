# Rfmalloc: Memory-Mapped File Allocation for R

Rfmalloc provides experimental memory-mapped file allocation
capabilities for R using a patched copy of the fmalloc library. The
current package exposes ALTREP file-backed vector allocation for
logical, integer, numeric, raw, complex, character, and list vectors
with fmalloc payload storage.

## Main Functions

- [`open_fmalloc`](https://sounkou-bioinfo.github.io/Rfmalloc/reference/open_fmalloc.md):

  Open an explicit fmalloc runtime handle.

- [`init_fmalloc`](https://sounkou-bioinfo.github.io/Rfmalloc/reference/init_fmalloc.md):

  Open and install a default fmalloc runtime.

- [`create_fmalloc_vector`](https://sounkou-bioinfo.github.io/Rfmalloc/reference/create_fmalloc_vector.md):

  Create vectors using fmalloc.

- [`create_fmalloc_matrix`](https://sounkou-bioinfo.github.io/Rfmalloc/reference/create_fmalloc_matrix.md):

  Create matrix-shaped fmalloc vectors.

- [`create_fmalloc_array`](https://sounkou-bioinfo.github.io/Rfmalloc/reference/create_fmalloc_array.md):

  Create array-shaped fmalloc vectors.

- [`create_fmalloc_data_frame`](https://sounkou-bioinfo.github.io/Rfmalloc/reference/create_fmalloc_data_frame.md):

  Create data.frames from fmalloc-backed columns.

- [`as_fmalloc_matrix`](https://sounkou-bioinfo.github.io/Rfmalloc/reference/as_fmalloc_matrix.md):

  Convert fmalloc vectors to matrix-shaped objects.

- [`as_fmalloc_array`](https://sounkou-bioinfo.github.io/Rfmalloc/reference/as_fmalloc_array.md):

  Convert fmalloc vectors to array-shaped objects.

- [`as_fmalloc_data_frame`](https://sounkou-bioinfo.github.io/Rfmalloc/reference/as_fmalloc_data_frame.md):

  Convert fmalloc-backed objects to a data.frame.

- [`fmalloc_linalg`](https://sounkou-bioinfo.github.io/Rfmalloc/reference/fmalloc_linalg.md):

  Matrix products for fmalloc-backed vectors and matrices.

- [`list_fmalloc_allocations`](https://sounkou-bioinfo.github.io/Rfmalloc/reference/list_fmalloc_allocations.md):

  List persistent allocation catalog records.

- [`diagnose_fmalloc_runtime`](https://sounkou-bioinfo.github.io/Rfmalloc/reference/diagnose_fmalloc_runtime.md):

  Summarize persistent allocation catalog state and runtime diagnostics.

- [`fmalloc_api`](https://sounkou-bioinfo.github.io/Rfmalloc/reference/fmalloc_api.md):

  Inspect runtimes and vectors through the public R/native API.

- [`cleanup_fmalloc`](https://sounkou-bioinfo.github.io/Rfmalloc/reference/cleanup_fmalloc.md):

  Request cleanup of an fmalloc runtime.

## Current Scope

- ALTREP file-backed allocation for logical, integer, numeric, raw,
  complex, character, and list vectors. List elements are restricted to
  `NULL` or Rfmalloc-backed vectors from the same runtime.

- Large allocations spanning multiple fmalloc chunks.

- Multiple runtime handles in one R process.

- Persistent and scratch runtime modes.

- Reference serialization for persistent fixed-width atomic and
  character ALTREP vectors.

- Fmalloc-backed ALTREP subset copies for vector indexing operations.

- Elementwise arithmetic/comparison/logical `Ops` and fmalloc-backed
  `matrixOps` products for `%*%`,
  [`crossprod()`](https://rdrr.io/r/base/crossprod.html), and
  [`tcrossprod()`](https://rdrr.io/r/base/crossprod.html).

- An in-file allocation catalog for persistent vectors.

- A C-callable API and installed header for other packages.

- Native lifetime tracking so runtime mappings outlive reachable vectors
  allocated from them.

- Runtime and catalog diagnostics for planning recovery and operational
  cleanup.

- R-facing API helpers for runtime/vector predicates, metadata, payload
  pointers, and version checks.

## Known Limitations

- ALTREP-backed dispatch now covers core `Ops`, `Summary`, `Math`,
  `Math2`, matrix `rowSums`/`colSums`/`rowMeans`/`colMeans`, and initial
  `matrixOps` workflows through S3 methods for common vector/matrix
  usage.

- Explicit base-fallback boundaries are:

  - [`rowSums()`](https://sounkou-bioinfo.github.io/Rfmalloc/reference/fmalloc_reduction_methods.md),
    [`colSums()`](https://sounkou-bioinfo.github.io/Rfmalloc/reference/fmalloc_reduction_methods.md),
    [`rowMeans()`](https://sounkou-bioinfo.github.io/Rfmalloc/reference/fmalloc_reduction_methods.md),
    and
    [`colMeans()`](https://sounkou-bioinfo.github.io/Rfmalloc/reference/fmalloc_reduction_methods.md)
    when the input is not an exact 2D matrix or `dims != 1L`; these
    cases now emit a warning and call the corresponding `base::`
    reducer.

  - Scalar or zero-length results from `Summary`, `Math`, and `Math2`
    generics (for example `sum(x)` returning a single value) are
    returned as ordinary R scalars by design.

  - Matrix products are currently computed by managed R loops so they
    preserve fmalloc-backed results before native BLAS kernels are
    added.

- Full operator- and method-family coverage is still incomplete for all
  R generics. Some advanced families may still materialize ordinary R
  objects in a few edge cases.

## Future Work

Future work includes native BLAS/LAPACK-backed matrix kernels,
view-based subset representations, catalog compaction and reset tooling,
metadata storage for attributes on persisted elements, robust
nested-list reference validation, and compaction of recovery metadata.

## See also

Useful links:

- <https://github.com/sounkou-bioinfo/Rfmalloc>

- <https://sounkou-bioinfo.github.io/Rfmalloc/>

- Report bugs at <https://github.com/sounkou-bioinfo/Rfmalloc/issues>

## Author

**Maintainer**: Sounkou Mahamane Toure <sounkoutoure@gmail.com>

Other contributors:

- Kenichi Yasukata (fmalloc) \[copyright holder\]

- Wolfram Gloger (ptmalloc3) \[copyright holder\]

- Free Software Foundation, Inc. (selected GNU C Library support files)
  \[copyright holder\]
