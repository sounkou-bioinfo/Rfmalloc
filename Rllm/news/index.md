# Changelog

## Rllm 0.1.0 (unreleased)

- Added a data-only architecture program traced from ordinary R modules
  and base pipes. Typed parameter identities, residual branches, named
  representation taps, multiple carried states and structured shared
  loops remain inspectable and serializable. ESM, StripedHyena2 and Tiny
  Recursive Model sketches stress those forms without claiming
  unsupported execution.

- GGUF metadata is normalized into semantic plans for llama, LFM2MoE and
  EmbeddingGemma before any weight is borrowed. The model-neutral
  lowerer covers causal, bidirectional and symmetric-window attention,
  normal and NEOX RoPE, post-branch RMS normalization, SwiGLU, GEGLU,
  short convolution, sparse routed experts, mean pooling and dense
  embedding projections.

- Added
  [`rllm_embed()`](https://sounkou-bioinfo.github.io/Rfmalloc/Rllm/reference/rllm_embed.md)
  and pinned EmbeddingGemma against both a pure-R f32 oracle and a real
  300M Q8_0 model compared with upstream GGML execution.

- Added the CUDA transformer path over Rggml’s official backend. A
  model-owned context uploads codec-native weights once and reuses them;
  mutable inputs, cache state and logits use the backend-neutral
  transfer API. The RTX 5050 suite pins whole-batch and incremental
  logits, plain and fmalloc caches, and CPU/CUDA cache handoff. A
  12-token prompt plus 128 greedy tokens measured a median 40.2 tok/s on
  CPU and 69.7 tok/s on CUDA after upload. A persistent GGML scheduler
  and graph-capture experiment measured 63.7 and 62.6 tok/s and was
  removed rather than retained as unused machinery.

- [`rllm_gguf_model()`](https://sounkou-bioinfo.github.io/Rfmalloc/Rllm/reference/rllm_gguf_model.md)
  now borrows every weight directly from the read-only GGUF mapping. It
  no longer copies all two-dimensional weights into an Rfmalloc file or
  dequantizes and repacks one-dimensional norms. The model retains the
  mapping, and GGML receives the original encoded pointers.

- [`rllm_generate()`](https://sounkou-bioinfo.github.io/Rfmalloc/Rllm/reference/rllm_generate.md)
  gained **sampling**: `temperature` (0 = greedy, the default, so
  existing behaviour is unchanged), `top_k`, `top_p` (nucleus), and
  `seed` for reproducibility. Greedy stays deterministic; sampled
  decoding is reproducible under a fixed seed. The sampler is pure logic
  over the logits vector (`test_sampling.R`).

- Added **incremental decoding with a KV cache** and the
  **bytes-boundary generation API**.
  [`rllm_kv_cache()`](https://sounkou-bioinfo.github.io/Rfmalloc/Rllm/reference/rllm_kv_cache.md)
  allocates per-layer f32 cache slabs (plain R memory, or fmalloc-backed
  when given a runtime - the cache as a disk citizen; a quantized cache
  codec can later replace the slabs without touching the graph).
  `rllm_forward(cache =)` appends new keys/values and attends over
  everything cached (llama.cpp’s classic layout: K flat, V transposed),
  advancing `n_past` by reference.
  [`rllm_generate()`](https://sounkou-bioinfo.github.io/Rfmalloc/Rllm/reference/rllm_generate.md)
  prefills the cache and decodes greedily one token per step; on
  SmolLM2-135M the current CPU path measures 40.2 tok/s for a 128-token
  controlled run. The correctness invariant - incremental logits equal
  whole-batch logits at every position - is pinned on the synthetic
  model for plain and fmalloc cache backings (`test_kv_cache.R`).

- The model I/O boundary is **bytes, not text**:
  [`rllm_encode()`](https://sounkou-bioinfo.github.io/Rfmalloc/Rllm/reference/rllm_decode.md)/
  [`rllm_decode()`](https://sounkou-bioinfo.github.io/Rfmalloc/Rllm/reference/rllm_decode.md)
  convert raw bytes to and from token ids using only the GGUF’s own
  byte-level BPE metadata (`tokenizer.ggml.tokens`/`merges`, GPT-2 byte
  alphabet) - no external tokenizer;
  [`rawToChar()`](https://rdrr.io/r/base/rawConversion.html) is the
  caller’s interpretation. Encoding applies merge ranks without GPT-2’s
  regex pre-tokenizer, so splits may occasionally differ from
  llama.cpp’s canonical ones while always decoding back to the same
  bytes. Real-model record: “The capital of France is Paris. The capital
  of Germany is” continues ” Berlin. The capital of Italy is Rome. The
  capital of Spain is Madrid.”

- **Validated on a real model** (SmolLM2-135M `Q4_K_M`, 30 layers, GQA
  9:3, 272 tensors in a `q4_k`/`q5_0`/`q6_k`/`q8_0`/`f32` mix): with the
  model’s decoded weights, the GGML graph matches a pure-R reference
  forward to 1e-06 relative (f32-twin roundtrip written with Rgguf’s own
  writer), and the native quantized path agrees on the argmax; its ~0.19
  relative logit deviation is Q8_K-activation/quantized-weight
  arithmetic compounded over 30 layers, not graph error. An opt-in smoke
  test (`RLLM_TEST_GGUF=<path>`, `test_real_model.R`) exercises the
  loader and graph on real files without affecting CI/CRAN.

- `q5_0`, `q5_1`, `q3_k`, and `q5_k` remain accepted by quantization and
  the typed-GEMM bridge. Their Rfmalloc codec registration now lives
  with every other GGUF storage codec in Rgguf and uses Rggml’s
  reference decoder.

- Added the **llama-architecture forward pass**:
  [`rllm_gguf_model()`](https://sounkou-bioinfo.github.io/Rfmalloc/Rllm/reference/rllm_gguf_model.md)
  loads a GGUF model’s hyperparameters and exposes weights in their
  native `q4_k`/`f32`/… encoding, and
  [`rllm_forward()`](https://sounkou-bioinfo.github.io/Rfmalloc/Rllm/reference/rllm_forward.md)
  assembles the GGML compute graph (RMSNorm, RoPE, causal self-attention
  with grouped-query support, SwiGLU) from Rggml’s graph-op C-callables
  over those weights zero-copy, computing the logits for every position
  of a token batch on the GGML CPU backend. Quantized weights are
  contracted through the SIMD-dispatched quantized kernels without ever
  being decoded to double. Verified against a pure-R reference
  implementation of the same arithmetic on a synthetic GGUF model
  written at test time - logits agree to float accumulation error (\<
  1e-4 relative) for both multi-head and grouped-query configurations,
  plus causality probes (`test_llama_forward.R`).

- Initial release. Rllm is the composition layer of the Rfmalloc
  ecosystem, wiring together Rfmalloc (file-backed storage), Rgguf (GGUF
  weights as fmalloc tensors) and Rggml (vendored GGML compute with
  runtime-SIMD-dispatched quantized kernels).

- Registers Rggml as an **Rfmalloc codec-aware (“typed”) matrix-multiply
  backend** named `"ggml"`, selected on load (toggle with
  [`rllm_use_ggml()`](https://sounkou-bioinfo.github.io/Rfmalloc/Rllm/reference/rllm_use_ggml.md)).
  When active, a product `dense %*% quantized_tensor` (the tensor being
  an `fmalloc_tensor` of a GGUF quantized codec: `q4_0`, `q4_1`, `q8_0`,
  `q2_k`, `q4_k`, `q6_k`) is computed natively in quantized space:
  Rfmalloc hands the raw compressed payload to Rllm, which points a GGML
  tensor at it zero-copy and contracts each weight row through GGML’s
  SIMD-dispatched `vec_dot`, quantizing the dense operand on the fly -
  no decode to `double`. Orientations and codecs the ggml path cannot
  serve (tensor on the left, non-quantized codecs) are declined and fall
  back to Rfmalloc’s decode-then-BLAS path, so results are always
  correct regardless of the selected backend.

- [`rllm_quantize_tensor()`](https://sounkou-bioinfo.github.io/Rfmalloc/Rllm/reference/rllm_quantize_tensor.md)
  encodes a dense matrix into a GGUF quantized block format and stores
  the payload in Rfmalloc-backed storage, returning an
  `fmalloc_tensor` - the write-side counterpart to Rgguf’s
  `gguf_tensor(..., as = "native")`.
