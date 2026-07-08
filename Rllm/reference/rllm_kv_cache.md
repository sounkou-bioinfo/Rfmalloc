# Create a KV cache for incremental decoding

Allocates the per-layer key/value cache slabs an incremental
[`rllm_forward()`](https://sounkou-bioinfo.github.io/Rfmalloc/Rllm/reference/rllm_forward.md)
writes into and attends over. Each slab is a raw vector of
`n_ctx * (n_embd / n_head) * n_head_kv` f32 values - plain R memory by
default, or Rfmalloc-backed (file-backed, memory-mapped) when a
`runtime` is given, which makes the cache a disk citizen: it survives in
the runtime's file and its pages are evictable like any other fmalloc
payload. (A quantized cache codec in the TurboQuant/PolarQuant vein can
later replace the f32 slabs without touching the graph.)

## Usage

``` r
rllm_kv_cache(model, n_ctx = 512L, runtime = NULL)
```

## Arguments

- model:

  An `rllm_model` from
  [`rllm_gguf_model()`](https://sounkou-bioinfo.github.io/Rfmalloc/Rllm/reference/rllm_gguf_model.md).

- n_ctx:

  Maximum number of positions the cache can hold.

- runtime:

  Optional
  [`Rfmalloc::open_fmalloc()`](https://rdrr.io/pkg/Rfmalloc/man/open_fmalloc.html)
  runtime for the slabs.

## Value

An environment of class `rllm_kv_cache` with fields `k`, `v` (per-layer
lists of raw vectors), `n_ctx`, and `n_past`.

## Details

The returned object is an environment, so
[`rllm_forward()`](https://sounkou-bioinfo.github.io/Rfmalloc/Rllm/reference/rllm_forward.md)
can advance its `n_past` by reference.

## See also

[`rllm_forward()`](https://sounkou-bioinfo.github.io/Rfmalloc/Rllm/reference/rllm_forward.md),
[`rllm_generate()`](https://sounkou-bioinfo.github.io/Rfmalloc/Rllm/reference/rllm_generate.md)
