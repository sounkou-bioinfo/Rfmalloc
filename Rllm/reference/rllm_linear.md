# Typed linear, normalization, attention and pooling operators

These are convenient typed constructors over
[`rllm_op()`](https://sounkou-bioinfo.github.io/Rfmalloc/Rllm/reference/rllm_input.md).
They keep the surface close to an R module's `forward` method while
recording explicit parameter identities and semantic attributes in the
frozen program.

## Usage

``` r
rllm_linear(x, weight, bias = NULL)

rllm_norm(x, weight, kind = c("rms", "layer"), eps = 1e-05, bias = NULL)

rllm_attention(
  x,
  query,
  key,
  value,
  output,
  heads,
  kv_heads = heads,
  query_norm = NULL,
  key_norm = NULL,
  rope = NULL,
  mask = list(type = "causal"),
  scale = NULL
)

rllm_pool(x, kind = c("mean", "cls", "none"))
```

## Arguments

- x:

  A traced `rllm_value`.

- weight, bias:

  Tensor references from
  [`rllm_parameter()`](https://sounkou-bioinfo.github.io/Rfmalloc/Rllm/reference/rllm_input.md).
  `bias` is optional.

- kind:

  Operator kind.

- eps:

  Normalization epsilon.

- query, key, value, output:

  Projection parameters for attention.

- heads, kv_heads:

  Query and key/value head counts.

- query_norm, key_norm:

  Optional per-head normalization parameters.

- rope, mask, scale:

  Data-only attention specifications.

## Value

A traced `rllm_value`.
