# Inspect the normalized checkpoint description of a GGUF model

The executable
[`rllm_program()`](https://sounkou-bioinfo.github.io/Rfmalloc/Rllm/reference/rllm_input.md)
is the source of truth. `rllm_plan()` derives a compact layer-oriented
inspection view from the program's validated GGML lowering. Constructing
or loading a model does not retain this second representation.

## Usage

``` r
rllm_plan(x)
```

## Arguments

- x:

  An `rllm_model` or a path to a GGUF file.

## Value

An inspectable object of class `rllm_plan`.
