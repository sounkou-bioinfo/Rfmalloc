# Inspect the normalized checkpoint description of a GGUF model

Rllm normalizes model-family metadata into a small data AST before it
borrows any weight payload. The plan names tensor roles, resolved
shapes, layer operators, feed-forward operators, persistent state and
output projection, and carries the semantic
[`rllm_program()`](https://sounkou-bioinfo.github.io/Rfmalloc/Rllm/reference/rllm_input.md)
derived from them. It contains data only: no R closures and no backend
objects. Native execution consumes the bound program; this plan remains
an inspection view of checkpoint normalization.

## Usage

``` r
rllm_plan(x)
```

## Arguments

- x:

  An `rllm_model` or a path to a GGUF file.

## Value

An inspectable object of class `rllm_plan`.
