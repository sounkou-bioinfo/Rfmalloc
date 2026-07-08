# Convert between token ids and raw bytes

The ids \<-\> bytes edge codecs of the bytes-boundary API, built from
the model's own GGUF tokenizer metadata (byte-level BPE,
`tokenizer.ggml.model == "gpt2"`). `rllm_decode()` maps ids to the exact
bytes they stand for. `rllm_encode()` byte-pair-encodes bytes into ids
using the file's merge ranks; it applies the merges without GPT-2's
regex pre-tokenizer, so a tokenization may occasionally differ from
llama.cpp's canonical split - every output is still a valid encoding of
the input (`rllm_decode(rllm_encode(x))` is always `x`).

## Usage

``` r
rllm_decode(model, ids)

rllm_encode(model, x)
```

## Arguments

- model:

  An `rllm_model` whose GGUF carries a byte-level BPE tokenizer.

- ids:

  Integer vector of 0-based token ids.

- x:

  A raw vector (or a single string, converted with
  [`charToRaw()`](https://rdrr.io/r/base/rawConversion.html)).

## Value

`rllm_decode()`: a raw vector. `rllm_encode()`: an integer vector of
0-based token ids.
