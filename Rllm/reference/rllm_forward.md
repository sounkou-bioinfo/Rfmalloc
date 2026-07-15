# Lower a semantic model plan and return its logits

Lowers the model's inspectable
[`rllm_plan()`](https://sounkou-bioinfo.github.io/Rfmalloc/Rllm/reference/rllm_plan.md)
to a GGML graph over its memory-mapped weights and computes it on a
chosen backend. The operator vocabulary includes attention, gated short
convolution, dense gated products and sparse routed experts. Quantized
weights are contracted natively in their encoded form - they are never
decoded to double. The CPU backend borrows the mapped bytes directly. On
its first use, the CUDA backend creates a model-owned context and
uploads the codec-native weights once. Later passes reuse those resident
weights; mutable inputs, cache slabs and logits move through Rggml's
transfer API.

## Usage

``` r
rllm_forward(model, tokens, cache = NULL, backend = c("cpu", "cuda"))
```

## Arguments

- model:

  An `rllm_model` from
  [`rllm_gguf_model()`](https://sounkou-bioinfo.github.io/Rfmalloc/Rllm/reference/rllm_gguf_model.md).

- tokens:

  Integer vector of 0-based token ids (as in the GGUF vocab).

- cache:

  Optional
  [`rllm_kv_cache()`](https://sounkou-bioinfo.github.io/Rfmalloc/Rllm/reference/rllm_kv_cache.md)
  for incremental decoding.

- backend:

  Compute backend. `"cpu"` is the zero-copy mmap path; `"cuda"` requires
  Rggml installed with `--with-cuda` and a visible NVIDIA device.

## Value

A numeric matrix of logits, dim `c(n_vocab, length(tokens))`: column `i`
scores the token following position `i`.

## Details

Without a `cache`, the graph attends over the whole token batch with a
causal mask (prompt scoring). With a
[`rllm_kv_cache()`](https://sounkou-bioinfo.github.io/Rfmalloc/Rllm/reference/rllm_kv_cache.md),
the pass appends the new tokens' keys/values to the cache and attends
over everything cached so far, advancing `cache$n_past` - the
incremental-decoding path: prefill once with the prompt, then feed one
token at a time.
