# Load a llama-architecture GGUF model for forward passes

Reads the hyperparameters and weights of a llama-architecture 'GGUF'
file into an `rllm_model` object usable with
[`rllm_forward()`](https://sounkou-bioinfo.github.io/Rfmalloc/Rllm/reference/rllm_forward.md).
2-d weight tensors are imported with
`Rgguf::gguf_tensor(as = "native")`: their payloads keep the GGUF
storage density (still `q4_k`/`f32`/... encoded) in Rfmalloc-backed,
memory-mapped storage, and the forward pass points GGML tensors at them
zero-copy. 1-d norm weights are small and are staged as packed `f32`
buffers.

## Usage

``` r
rllm_gguf_model(path, runtime = NULL, rope_mode = 0L)
```

## Arguments

- path:

  Path to a GGUF file.

- runtime:

  Optional
  [`Rfmalloc::open_fmalloc()`](https://sounkou-bioinfo.github.io/Rfmalloc/Rfmalloc/reference/open_fmalloc.html)
  runtime for the weight payloads; `NULL` uses Rfmalloc's default
  runtime.

- rope_mode:

  RoPE flavour: `0` (normal/interleaved, llama) or `2` (NEOX-style, e.g.
  qwen2). Defaults to `0`.

## Value

An object of class `rllm_model`: a list with `hparams` (named numeric
list), `tensors` (named list of weight payloads), and `rope_mode`.

## Details

The loader expects the standard llama tensor names (`token_embd.weight`,
`blk.<i>.attn_q.weight`, ..., `output_norm.weight`) and hyperparameter
keys (`<arch>.block_count`, `<arch>.embedding_length`, ...). Models with
tied embeddings (no `output.weight`) reuse `token_embd.weight` as the
output projection.

## See also

[`rllm_forward()`](https://sounkou-bioinfo.github.io/Rfmalloc/Rllm/reference/rllm_forward.md)
