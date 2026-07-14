# Experiments

These are falsifiable probes of the current abstractions, not release gates.

## ALP versus native GGUF on CPU

`alp_gguf_cpu.R` compares five paths over one real two-dimensional `q4_k`
weight:

1. dense `f64` with OpenBLAS;
2. the current lossless ALP codec followed by BLAS;
3. a decimal-rounded control where ALP should compress well;
4. Q4_K decoded to doubles followed by BLAS;
5. the original GGUF Q4_K bytes contracted by GGML's native quantized kernel.

Run it with:

```sh
R_LIBS=/tmp/rfmalloc-work-lib \
Rscript experiments/alp_gguf_cpu.R model.gguf blk.2.attn_q.weight
```

The 2026-07-10 development-server run used a 2048 x 2048 weight from
`LFM2.5-8B-A1B-Q4_K_M.gguf` and OpenBLAS:

| Storage | Bytes | Bits/value |
|---|---:|---:|
| dense f64 | 33,554,432 | 64.0 |
| ALP over model-like values | 33,652,752 | 64.2 |
| ALP over a three-decimal control | 3,725,840 | 7.1 |
| Q4_K | 2,359,296 | 4.5 |

| Batch | Path | Median ms |
|---:|---|---:|
| 1 | dense BLAS | 2.25 |
| 1 | ALP model-like values plus BLAS | 20.75 |
| 1 | ALP decimal control plus BLAS | 37.30 |
| 1 | Q4_K decode plus BLAS | 27.25 |
| 1 | native Q4_K GGML | 0.3 |
| 32 | dense BLAS | 4.20 |
| 32 | ALP model-like values plus BLAS | 23.20 |
| 32 | ALP decimal control plus BLAS | 33.00 |
| 32 | Q4_K decode plus BLAS | 24.80 |
| 32 | native Q4_K GGML | 6.40 |

The native Q4_K result differed from its dequantized-weight reference by about
0.0066 relative at batch 1. That difference includes GGML's activation
quantization and is not a codec-consistency failure.

The current ALP implementation does not support the original LLM bet. Its
decimal transform falls back to raw chunks on model-like binary floating-point
values, and its scalar decode into an f64 panel is nowhere near memory
bandwidth. The useful research forks are now explicit:

- add the binary-float ALP-RD path or another lossless float transform;
- vectorize decode and measure decoded GB/s independently of GEMM;
- add a compressed dot kernel if batch-1 decode matters, because decode into
  an f64 scratch panel followed by BLAS cannot compete with GGML's fused Q4_K
  dot path;
- retain ALP for decimal analytical arrays even if the LLM-weight hypothesis
  is rejected.

## SSD streaming

GGUF tensors can now be borrowed as read-only storage views. Loading an Rllm
model no longer copies every weight into a second fmalloc file. The view keeps
the original mapping alive, and `fmalloc_storage_advise()` provides
`sequential`, `willneed`, and `dontneed` page intentions over any typed span.

As a setup check on the same 4.9 GB model, `gguf_import(as = "view")` created
256 tensor views covering 4.79 GiB of encoded weights in 34 ms and added zero
fmalloc allocation records. This measures cached metadata setup, not SSD
throughput, but it proves the former whole-model copy is gone.

This is more general than embedding ds4's model loop in the allocator, but it
does not yet guarantee better cold-SSD inference. A model larger than RAM is
bounded by storage bandwidth either way. The remaining experiment requires a
layer scheduler which emits the access sequence, prefetches the next layer,
and releases the previous layer. Only then can mmap plus advice be compared
fairly with ds4-style `pread` double buffering under a controlled page-cache
state.
