# Rllm 0.1.0 (unreleased)

- Initial release. Rllm is the composition layer of the Rfmalloc ecosystem,
  wiring together Rfmalloc (file-backed storage), Rgguf (GGUF weights as
  fmalloc tensors) and Rggml (vendored GGML compute with runtime-SIMD-dispatched
  quantized kernels).
- Registers Rggml as an **Rfmalloc codec-aware ("typed") matrix-multiply
  backend** named `"ggml"`, selected on load (toggle with `rllm_use_ggml()`).
  When active, a product `dense %*% quantized_tensor` (the tensor being an
  `fmalloc_tensor` of a GGUF quantized codec: `q4_0`, `q4_1`, `q8_0`, `q2_k`,
  `q4_k`, `q6_k`) is computed natively in quantized space: Rfmalloc hands the
  raw compressed payload to Rllm, which points a GGML tensor at it zero-copy
  and contracts each weight row through GGML's SIMD-dispatched `vec_dot`,
  quantizing the dense operand on the fly - no decode to `double`. Orientations
  and codecs the ggml path cannot serve (tensor on the left, non-quantized
  codecs) are declined and fall back to Rfmalloc's decode-then-BLAS path, so
  results are always correct regardless of the selected backend.
- `rllm_quantize_tensor()` encodes a dense matrix into a GGUF quantized block
  format and stores the payload in Rfmalloc-backed storage, returning an
  `fmalloc_tensor` - the write-side counterpart to Rgguf's
  `gguf_tensor(..., as = "native")`.
