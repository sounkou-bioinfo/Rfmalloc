# Run a transformer forward pass and return the logits

Assembles the GGML compute graph for a llama-architecture forward pass
(RMSNorm, RoPE, causal self-attention, SwiGLU feed-forward) over the
model's memory-mapped weights and computes it on the GGML CPU backend.
Quantized weights are contracted natively through the SIMD-dispatched
quantized kernels - they are never decoded to double.

## Usage

``` r
rllm_forward(model, tokens, cache = NULL)
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
