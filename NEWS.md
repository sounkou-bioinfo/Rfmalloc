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
- Typed/compressed tensors are now genuinely n-dimensional:
  `create_fmalloc_tensor()`/`as_fmalloc_tensor()` accept dims of any rank and
  `fmalloc_tensor_materialize()` returns an array of that shape (storage and
  decoding are dimension-agnostic). The matrix products (`%*%`, `crossprod()`,
  `tcrossprod()`) require exactly 2 dimensions and error clearly otherwise.
- Added a builtin `"sparse"` tensor codec for mostly-zero data (e.g.
  single-cell counts): `as_fmalloc_tensor(x, dtype = "sparse")` stores only the
  nonzeros of each 1024-element chunk, losslessly, and the resulting tensor
  participates in the panel-streamed matrix products like any other codec. At
  ~10% density it compresses ~6x versus dense `f64` while `%*%`/`crossprod`
  stay exact — the storage layer for out-of-core single-cell/genomics matrices.
- Added a builtin, lossless `"alp"` tensor codec and `as_fmalloc_tensor()`:
  double vectors/matrices are compressed into fmalloc storage as bit-packed
  decimal-scaled integers in independently decodable 1024-value chunks
  (Afroozeh et al., ALP, \doi{10.1145/3626717}; scalar core adapted from the
  MIT-licensed zap implementation — see `inst/COPYRIGHTS`), with exact-value
  patches, a raw escape hatch for incompressible chunks, and
  division-by-exact-power-of-ten decoding so decimal-rounded data
  round-trips with few patches. Compressed tensors participate in the
  panel-streamed matrix products like any other typed tensor.
- Added `fmalloc_matmul_ooc()`, an out-of-core matrix product for fmalloc
  matrices larger than RAM: `A %*% x` consumes `A` one contiguous column tile
  at a time through BLAS `dgemm`, then releases each tile's pages with
  `madvise(MADV_DONTNEED)` (and hints the payload `MADV_SEQUENTIAL`), so the
  resident set stays bounded by the tile budget. Demonstrated on a 62.6 GB
  matrix (equal to total RAM): peak resident memory 0.31 GB during the gemv,
  result exact vs the analytic reference.
- Added a genomics layer: `fmalloc_pca()` (out-of-core truncated PCA via the
  Gram matrix — `crossprod()` + eigendecomposition + projection) and
  `fmalloc_colVars()`/`fmalloc_rowVars()` (variance reductions for
  highly-variable-feature selection). The heavy steps dispatch through the
  pluggable matrix-multiply backend, so single-cell PCA on a larger-than-RAM,
  compressed count matrix runs on CPU BLAS today and on a registered GPU
  backend unchanged — the same call, composable across backends.
- Extended the backend registry with a codec-aware *typed* hook (C API v6,
  `Rfmalloc_register_matmul_backend_ex`): a backend can register a
  `typed_gemm` that receives a compressed tensor's raw codec payload and dims
  and multiplies it by a dense operand *without* Rfmalloc decoding to f64 —
  enabling native quantized/on-device matmul (e.g. an fmalloc-mmap'd `q4_k`
  payload is byte-compatible with a ggml Q4_K tensor). A typed backend may
  decline a codec, in which case Rfmalloc falls back to the panel-decode path.
- Added a pluggable matrix-multiply backend registry: the matrix-product
  kernels (`%*%`, `crossprod()`, `tcrossprod()`, the out-of-core and
  typed-tensor products) dispatch their `dgemm` through a selectable backend.
  `fmalloc_matmul_backend(name)` selects one (default `"blas"` = R's BLAS) and
  `fmalloc_matmul_backends()` lists registered ones; downstream packages
  register a GEMM kernel (e.g. GPU or out-of-core-aware) through the new
  `Rfmalloc_register_matmul_backend` C-callable (API version 5). Selection is
  Rfmalloc-scoped (base R's `%*%` is unaffected), and a backend may decline a
  call to fall back to BLAS. Together with the tensor codec registry this makes
  the stack pluggable at both tiers — how bytes decode and what hardware
  multiplies — over fmalloc storage.
- Added `fmalloc_sync()` to flush a persistent runtime's backing store to disk
  (`msync`/`fsync`). Writes to the `MAP_SHARED` mapping (including in-place
  mutations) are otherwise only written back asynchronously by the OS, so a
  crash before writeback loses unsynced data; `fmalloc_sync()` is the explicit
  durability barrier. No-op where `msync` is unavailable (Windows/Rtools).
- Added in-place (by-reference) mutation: `fmalloc_set(x, i, value)` and
  `fmalloc_fill(x, value)` write straight through the backing store, mutating
  by reference regardless of sharing. (An ordinary `x[i] <- value` on an
  *unshared* fmalloc vector already writes in place via the ALTREP data
  pointer; the copy that hurts is R's copy-on-modify when the vector is
  *shared* — these functions bypass it.) The functions are deliberately
  explicit (never a silent `[<-` method) because
  they break value semantics by design: all bindings to the same vector
  observe the change, and for a persistent runtime the durable store is
  updated. Supported for atomic logical/integer/numeric/complex/raw vectors.
  `fmalloc_add()`/`fmalloc_sub()`/`fmalloc_mul()`/`fmalloc_div()` compute
  `x <- x op y` in place (numeric vectors), for the accumulate-into-`x` pattern
  without per-step allocation.
- Added `fmalloc_tcrossprod_ooc()` and out-of-core routing for `tcrossprod(X)`
  (`X X'`): tiles over the reduction dimension (columns of `X`), accumulating
  rank-`kw` updates via `dgemm('N','T')`. Each contiguous column panel is read
  once and evicted, so it is a single streaming pass over `X` (input residency
  ~one panel) with an fmalloc-backed `m x m` result. Single-argument
  `tcrossprod(X)` on a real fmalloc matrix auto-routes above the threshold.
- Added `fmalloc_crossprod_ooc()` and out-of-core routing for `crossprod(X)`:
  the Gram matrix `X'X` is computed as pairs of contiguous column panels of `X`
  through BLAS `dgemm`, writing each block straight into the `n x n` fmalloc
  result and releasing panels with `madvise`. Input residency stays ~two
  panels (so `X` may exceed RAM) and the result is fmalloc-backed (so the
  `n x n` Gram matrix may too) — the covariance/Gram operation behind PCA,
  ridge, and GWAS on larger-than-RAM matrices. Single-argument `crossprod(X)`
  on a real fmalloc matrix auto-routes above `Rfmalloc.ooc_threshold_gb`;
  two-argument and complex cases keep the in-core path.
- `%*%` on an fmalloc matrix now auto-selects the out-of-core path when the
  left operand's payload reaches `getOption("Rfmalloc.ooc_threshold_gb")`
  (default: half of physical RAM, detected portably via POSIX `sysconf` or
  BSD/macOS `sysctl`), tiling with `getOption("Rfmalloc.ooc_tile_mb", 256)`;
  smaller products keep the in-core BLAS path unchanged. Elementwise `Ops` and
  matrix reductions are left as-is: they are already single-pass streaming, so
  forcing page eviction on them would only regress the in-core case.
  `crossprod()`/`tcrossprod()` are not auto-routed because their output can
  itself exceed RAM.
- Typed/compressed tensor matrix products stream out-of-core the same way:
  above the OOC threshold, each column panel's compressed source pages are
  released after decoding (fixed-geometry codecs such as `f16`/`bf16`/`f32`
  and the quantized GGUF formats), so a tensor whose decoded `f64` form
  exceeds RAM multiplies with a bounded resident set. Demonstrated on a matrix
  that is 74.5 GB as `f64` (larger than RAM) stored as 18.6 GB of `f16`:
  matrix-vector product at 0.13 GB peak resident memory, exact vs the analytic
  reference. Adds `rfm_raw_fill_pattern_impl` for building large payloads.
- Added typed fmalloc tensors: `create_fmalloc_tensor()` tags an fmalloc raw
  payload with a dtype codec (builtin `f64`/`f32`/`f16`/`bf16`; other packages
  register codecs through the new `Rfmalloc_register_tensor_codec` C-callable,
  API version 4) and dims. Matrix products against dense double operands
  decode the payload in bounded block-aligned column panels streamed through
  BLAS `dgemm`, so the full double representation is never materialized;
  `fmalloc_tensor_materialize()` converts to a regular fmalloc matrix.
- Matrix products (`%*%`, `crossprod()`, `tcrossprod()`) now call BLAS `dgemm`
  for finite double operands, falling back to the managed native loops for
  `NA`/`NaN`/`Inf` values and logical/integer/complex operands (the same split
  base R's default matrix product uses).
- Fixed fmalloc vector recognition for R's generic ALTREP wrappers: attribute
  changes on referenced fmalloc vectors of length >= 64 (for example the class
  stripping done by dispatch helpers) made R substitute a wrapper object that
  native code no longer recognized, breaking `%*%`, `crossprod()`, and
  `tcrossprod()` for realistically sized matrices. Native lookup now unwraps
  such wrappers, and the linear algebra methods validate operands without
  stripping classes.
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
