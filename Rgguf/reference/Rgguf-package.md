# Rgguf: Read 'GGUF' Model Files into 'Rfmalloc'-Backed Arrays

Rgguf reads 'GGUF' model files, the file format used by the 'llama.cpp'
project to store large language model tensors and metadata, and exposes
their tensors as Rfmalloc-backed, file-backed ALTREP matrices and
arrays. Quantized tensor types supported by the vendored 'gguflib'
parser are dequantized on demand as they are read.

## Main Functions

- [`gguf_open`](https://sounkou-bioinfo.github.io/Rfmalloc/Rgguf/reference/gguf_open.md):

  Open a GGUF file and return a context handle.

- [`gguf_metadata`](https://sounkou-bioinfo.github.io/Rfmalloc/Rgguf/reference/gguf_metadata.md):

  Read all metadata key-value pairs.

- [`gguf_tensors`](https://sounkou-bioinfo.github.io/Rfmalloc/Rgguf/reference/gguf_tensors.md):

  List the tensor directory as a data frame.

- [`gguf_tensor`](https://sounkou-bioinfo.github.io/Rfmalloc/Rgguf/reference/gguf_tensor.md):

  Read and dequantize a single tensor into an Rfmalloc-backed array.

- [`gguf_import`](https://sounkou-bioinfo.github.io/Rfmalloc/Rgguf/reference/gguf_import.md):

  Read some or all tensors into a named list of Rfmalloc-backed arrays.

- [`gguf_write_tensors`](https://sounkou-bioinfo.github.io/Rfmalloc/Rgguf/reference/gguf_write_tensors.md):

  Write a minimal GGUF file (F32 tensors), mainly for round-trip
  testing.

## Design

Allocation happens on the R side: tensor destinations are created with
[`Rfmalloc::create_fmalloc_matrix()`](https://rdrr.io/pkg/Rfmalloc/man/create_fmalloc_matrix.html)/[`create_fmalloc_array()`](https://rdrr.io/pkg/Rfmalloc/man/create_fmalloc_array.html),
which returns a properly classed, file-backed ALTREP object with
Rfmalloc's full `Ops`/matrix-product dispatch already working. Native
code only ever fills that destination in place (dequantizing as it
goes); it never allocates R vectors of tensor size itself.

## Known Limitations

- Not every GGUF tensor type is dequantizable: the vendored 'gguflib'
  parser implements dequantization for `f32`, `f16`, `bf16`, `f64`, the
  plain integer types, and the `q8_0`, `q4_0`, `q4_1`, `q2_k`, `q4_k`,
  and `q6_k` quantized block formats. Other quantized formats (e.g.
  `q5_0`, `q5_1`, `q3_k`, `q5_k`, `q8_k`, and the `iq*` formats) are not
  supported and
  [`gguf_tensor`](https://sounkou-bioinfo.github.io/Rfmalloc/Rgguf/reference/gguf_tensor.md)
  errors clearly for them.

- The vendored 'gguflib' parser memory-maps GGUF files read/write, so
  [`gguf_open`](https://sounkou-bioinfo.github.io/Rfmalloc/Rgguf/reference/gguf_open.md)
  requires the file to be writable even when only reading tensors from
  it.

- Dequantizing a quantized tensor type currently goes through an
  intermediate `malloc()`'d float buffer (freed immediately after
  widening to double); a future version could dequantize block-by-block
  directly into the destination.

## See also

Useful links:

- <https://github.com/sounkou-bioinfo/Rfmalloc>

- Report bugs at <https://github.com/sounkou-bioinfo/Rfmalloc/issues>

## Author

**Maintainer**: Sounkou Mahamane Toure <sounkoutoure@gmail.com>

Other contributors:

- Salvatore Sanfilippo (gguf-tools/gguflib) \[copyright holder\]

- Georgi Gerganov (GGUF format enums/structures adapted from ggml)
  \[copyright holder\]
