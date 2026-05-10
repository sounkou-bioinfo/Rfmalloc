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

- [`list_fmalloc_allocations`](https://sounkou-bioinfo.github.io/Rfmalloc/reference/list_fmalloc_allocations.md):

  List persistent allocation catalog records.

- `diagnose_fmalloc_runtime`:

  Summarize persistent allocation catalog state and runtime diagnostics.

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

- An in-file allocation catalog for persistent vectors.

- A C-callable API and installed header for other packages.

- Native lifetime tracking so runtime mappings outlive reachable vectors
  allocated from them.

- Runtime and catalog diagnostics for planning recovery and operational
  cleanup.

## Future Work

Future work includes view-based subset representations, catalog
compaction and reset tooling, metadata storage for attributes on
persisted elements, robust nested-list reference validation, and
compaction of recovery metadata.

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
