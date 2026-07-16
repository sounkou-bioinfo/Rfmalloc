# Compute one pooled sequence embedding

Lowers an embedding model's semantic program over a complete token
sequence. Bidirectional and symmetric-window attention are evaluated
without a decode cache, then the program's pooling and projection
pipeline produces one vector. Quantized weights remain encoded in the
mapped GGUF file.

## Usage

``` r
rllm_embed(model, tokens, normalize = TRUE, backend = c("cpu", "cuda"))
```

## Arguments

- model:

  An embedding `rllm_model` from
  [`rllm_gguf_model()`](https://sounkou-bioinfo.github.io/Rfmalloc/Rllm/reference/rllm_gguf_model.md).

- tokens:

  Integer vector of 0-based token ids.

- normalize:

  Whether to return a unit-length vector.

- backend:

  Compute backend. `"cpu"` uses mapped weights directly; `"cuda"`
  requires a CUDA-enabled Rggml installation.

## Value

A numeric vector whose length is the program's output dimension.

## Details

Tokenization is deliberately separate from model execution. Supply the
model's 0-based token ids, including any BOS or EOS tokens required by
its tokenizer.
