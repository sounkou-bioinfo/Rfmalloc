# Rllm 0.1.0 (unreleased)

- Added **incremental decoding with a KV cache** and the **bytes-boundary
  generation API**. `rllm_kv_cache()` allocates per-layer f32 cache slabs
  (plain R memory, or fmalloc-backed when given a runtime - the cache as a
  disk citizen; a quantized cache codec can later replace the slabs without
  touching the graph). `rllm_forward(cache =)` appends new keys/values and
  attends over everything cached (llama.cpp's classic layout: K flat,
  V transposed), advancing `n_past` by reference. `rllm_generate()` prefills
  the cache and decodes greedily one token per step; on SmolLM2-135M this
  measures ~16 tok/s, 8.5x over full re-forwards, with identical outputs.
  The correctness invariant - incremental logits equal whole-batch logits at
  every position - is pinned on the synthetic model for plain and fmalloc
  cache backings (`test_kv_cache.R`).
- The model I/O boundary is **bytes, not text**: `rllm_encode()`/
  `rllm_decode()` convert raw bytes to and from token ids using only the
  GGUF's own byte-level BPE metadata (`tokenizer.ggml.tokens`/`merges`, GPT-2
  byte alphabet) - no external tokenizer; `rawToChar()` is the caller's
  interpretation. Encoding applies merge ranks without GPT-2's regex
  pre-tokenizer, so splits may occasionally differ from llama.cpp's canonical
  ones while always decoding back to the same bytes. Real-model record:
  "The capital of France is Paris. The capital of Germany is" continues
  " Berlin. The capital of Italy is Rome. The capital of Spain is Madrid."
- **Validated on a real model** (SmolLM2-135M `Q4_K_M`, 30 layers, GQA 9:3,
  272 tensors in a `q4_k`/`q5_0`/`q6_k`/`q8_0`/`f32` mix): with the model's
  decoded weights, the GGML graph matches a pure-R reference forward to
  1e-06 relative (f32-twin roundtrip written with Rgguf's own writer), and
  the native quantized path agrees on the argmax; its ~0.19 relative logit
  deviation is Q8_K-activation/quantized-weight arithmetic compounded over
  30 layers, not graph error. An opt-in smoke test
  (`RLLM_TEST_GGUF=<path>`, `test_real_model.R`) exercises the loader and
  graph on real files without affecting CI/CRAN.
- Registered **GGML-backed Rfmalloc codecs** for the GGUF quantized types
  Rgguf's vendored gguflib cannot decode - `q5_0`, `q5_1`, `q3_k`, `q5_k`
  (real `Q4_K_M` model files are full of `q5_0` tensors). The decoder is
  GGML's reference `to_float` via `Rggml_dequantize`, so these codecs are
  consistent-by-construction with the compute path; block geometry is taken
  from the vendored GGML at registration. `rllm_quantize_tensor()` and the
  typed-GEMM bridge accept the new types too.
- Added the **llama-architecture forward pass**: `rllm_gguf_model()` loads a
  GGUF model's hyperparameters and weights (2-d tensors imported natively -
  still `q4_k`/`f32`/... encoded - into fmalloc-backed, memory-mapped
  storage), and `rllm_forward()` assembles the GGML compute graph (RMSNorm,
  RoPE, causal self-attention with grouped-query support, SwiGLU) from
  Rggml's graph-op C-callables over those weights zero-copy, computing the
  logits for every position of a token batch on the GGML CPU backend.
  Quantized weights are contracted through the SIMD-dispatched quantized
  kernels without ever being decoded to double. No KV cache yet (whole-batch
  causal attention: a prompt-scoring entry point, not incremental
  generation). Verified against a pure-R reference implementation of the
  same arithmetic on a synthetic GGUF model written at test time - logits
  agree to float accumulation error (< 1e-4 relative) for both multi-head
  and grouped-query configurations, plus causality probes
  (`test_llama_forward.R`).
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
