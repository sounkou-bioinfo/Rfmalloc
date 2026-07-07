# Rgguf 0.1.0 (unreleased)

- `gguf_tensor(as = "native")` no longer requires gguflib dequantization
  support for the tensor's type - a native import copies the raw,
  still-encoded payload, so it only needs a registered Rfmalloc codec (which
  another package may provide: Rllm registers GGML-backed codecs for `q5_0`/
  `q5_1`/`q3_k`/`q5_k`, all common in real `Q4_K_M` model files). The
  gguflib-support check now applies only to `as = "numeric"`, whose error
  message points at the native path.
- **Fixed a Q4_K dequantization bug inherited from upstream gguf-tools**: in
  `gguf_q4_k_to_float()`, the high-nibble half of every 64-weight group (the
  odd sub-blocks 1, 3, 5, 7 of each super-block) was dequantized with the
  *previous* sub-block's scale/min, making `q4_k` decodes ~33% wrong in
  relative Frobenius norm (GGML's reference uses scale `is+0` for the low
  nibbles and `is+1` for the high nibbles). Since `Q4_K_M` is the most common
  real-model quantization, this affected `gguf_tensor(as = "numeric")` and
  `fmalloc_tensor_materialize()` for typical GGUF files. All six registered
  quantized codecs (`q4_0`, `q4_1`, `q8_0`, `q2_k`, `q4_k`, `q6_k`) now decode
  **bit-identically** to GGML's own type-traits `to_float` reference, verified
  by a new regression test (`test_gguf_codec_ggml_ref.R`) against a committed
  fixture of GGML-quantized payloads and their GGML-decoded expected values.
- Added native typed imports: `gguf_tensor(as = "native")` (and
  `gguf_import(as = "native")`) copy a 2-d tensor's raw GGUF payload into
  fmalloc storage at its original density (e.g. 4.5 bits/weight for `q4_k`)
  and return an `Rfmalloc::fmalloc_tensor`, decoded in bounded panels only
  inside matrix products. Registers gguflib's `q4_0`/`q4_1`/`q8_0`/`q2_k`/
  `q4_k`/`q6_k` dequantizers as Rfmalloc tensor codecs (requires Rfmalloc
  C API version 4).
- Initial release.
- Added `gguf_open()` to memory-map a GGUF file (via a vendored copy of
  antirez's `gguflib`) and return a `gguf_ctx` handle with a closing
  finalizer.
- Added `gguf_metadata()` to read all metadata key-value pairs (scalars and
  arrays of the supported GGUF value types) into a named R list.
- Added `gguf_tensors()` to list the tensor directory (name, type, number of
  dimensions, dimensions, element count, byte size, file offset) as a data
  frame, without reading any tensor payload.
- Added `gguf_tensor()` to read and dequantize a single named tensor
  directly into an `Rfmalloc`-backed, file-backed ALTREP matrix/array of
  doubles: R allocates the destination via
  `Rfmalloc::create_fmalloc_matrix()`/`create_fmalloc_array()`, and native
  code fills it in place, so no ordinary R-sized copy of the tensor is ever
  materialized on the R heap.
- Added `gguf_import()` to read some or all tensors from a file into a named
  list of `Rfmalloc`-backed matrices/arrays sharing one open file handle.
- Added `gguf_write_tensors()`, a minimal GGUF writer (F32 tensors plus
  simple string/numeric metadata), primarily to build small GGUF fixtures
  for this package's own round-trip tests without shipping binary fixtures.
- Verified (with a dedicated round-trip test) that GGUF's `dim[0]`
  (fastest-varying/contiguous dimension) maps directly onto R's first,
  column-major dimension, so tensor dimensions need no transposition:
  `dim(gguf_tensor(x, name)) == c(dim[0], dim[1], ...)`.
- Direct (non-quantized) dequantization support for `f32`, `f16`, `bf16`,
  `f64`, and the plain `i8`/`i16`/`i32`/`i64` integer tensor types, plus
  quantized-block dequantization (via the vendored `gguflib`'s own
  `gguf_tensor_to_float()`) for `q8_0`, `q4_0`, `q4_1`, `q2_k`, `q4_k`, and
  `q6_k`. Other quantized formats (`q5_0`, `q5_1`, `q3_k`, `q5_k`, `q8_k`,
  and the `iq*` formats) are not implemented by the vendored parser and
  `gguf_tensor()` errors clearly for them instead of guessing.
- Documented (README, `inst/smoke_test.R`) a discovered `Rfmalloc` 0.1.0
  limitation, not an `Rgguf` bug: `%*%`/`crossprod`/`tcrossprod` on
  fmalloc-backed operands with >= 64 elements currently fail once the
  operand has passed through any R closure call (effectively always), due
  to a generic R-core ALTREP attribute-wrapping behavior that `Rfmalloc`
  does not unwrap. `gguf_tensor()`/`gguf_import()` themselves are
  unaffected at any size.
- `R CMD check` reports one NOTE about `printf`/`puts`/`putchar` calls found
  in `gguflib.o`; these come from vendored, unused pretty-printing helpers
  in `gguflib.c` (`gguf_print_value()` and friends, used only by upstream's
  CLI) that `Rgguf` never calls and does not export.
