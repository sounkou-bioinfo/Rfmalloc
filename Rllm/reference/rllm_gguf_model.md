# Load a GGUF model as a semantic execution plan and borrowed weights

Normalizes the model-family metadata into a typed
[`rllm_plan()`](https://sounkou-bioinfo.github.io/Rfmalloc/Rllm/reference/rllm_plan.md),
validates every tensor role and shape, then borrows each required weight
from the original GGUF mapping. Quantized and floating-point payloads
keep their on-disk encoding; no second weight store is created.

## Usage

``` r
rllm_gguf_model(path, runtime = NULL, rope_mode = NULL)
```

## Arguments

- path:

  Path to a GGUF file.

- runtime:

  [`Rfmalloc::open_fmalloc()`](https://sounkou-bioinfo.github.io/Rfmalloc/Rfmalloc/reference/open_fmalloc.html)
  runtime attached to the borrowed tensor views, or `NULL` to use the
  default established by
  [`Rfmalloc::init_fmalloc()`](https://sounkou-bioinfo.github.io/Rfmalloc/Rfmalloc/reference/init_fmalloc.html).
  It supplies the allocation context for operations which produce
  fmalloc results; the weight bytes remain in the GGUF mapping.

- rope_mode:

  Optional RoPE override: `0` for normal/interleaved or `2` for NEOX.
  The architecture plan supplies the default.

## Value

An object of class `rllm_model` containing its hyperparameters, borrowed
weight payloads, tokenizer metadata, and model-owned backend contexts
created lazily by
[`rllm_forward()`](https://sounkou-bioinfo.github.io/Rfmalloc/Rllm/reference/rllm_forward.md).

## Details

Architecture definitions are data ASTs rather than native model-family
branches. The registered plans cover llama, Qwen3.5, LFM2MoE and
EmbeddingGemma. Models with tied embeddings reuse `token_embd.weight` as
the output projection.

## See also

[`rllm_forward()`](https://sounkou-bioinfo.github.io/Rfmalloc/Rllm/reference/rllm_forward.md)
