# Rllm

Rllm is the **composition layer** of the
[Rfmalloc](https://github.com/sounkou-bioinfo/Rfmalloc) out-of-core
array stack: it wires (file-backed, memory-mapped storage), (GGUF model
files as fmalloc tensors) and (vendored GGML compute with
runtime-SIMD-dispatched quantized kernels) into LLM inference —
quantized weights stay in their GGUF encoding, memory-mapped from disk,
and are never decoded to double on the compute path.

Two things live here:

1.  a **codec-aware matmul backend**:
    `dense %*% quantized_fmalloc_tensor` runs natively in quantized
    space through GGML (registered with Rfmalloc’s backend registry and
    selected on load);
2.  a **llama-architecture inference engine**: GGUF loader, forward
    pass, KV cache, and byte-level generation.

## Quantized products, zero-copy (runs at render time)

``` r

library(Rllm)   # registers + selects the ggml backend
#> Rllm: ggml quantized matmul backend registered and active for Rfmalloc typed tensors (disable with rllm_use_ggml(FALSE)).

rt <- Rfmalloc::open_fmalloc(tempfile(fileext = ".bin"))
set.seed(1)
W  <- matrix(rnorm(512 * 8, sd = 0.4), nrow = 512)
Wq <- rllm_quantize_tensor(W, "q4_k", runtime = rt)  # 4.5 bits/weight, mmap'd
X  <- matrix(rnorm(4 * 512), nrow = 4)
Y  <- X %*% Wq                                       # GGML SIMD, no f64 decode
sqrt(sum((Y - X %*% W)^2) / sum((X %*% W)^2))        # q4_k-accurate
#> [1] 0.09047354
```

## Inference: bytes in, bytes out

The model I/O boundary is **token ids and raw bytes** — never character
vectors in the core.
[`rllm_encode()`](https://sounkou-bioinfo.github.io/Rfmalloc/Rllm/reference/rllm_decode.md)/[`rllm_decode()`](https://sounkou-bioinfo.github.io/Rfmalloc/Rllm/reference/rllm_decode.md)
are edge codecs built from the GGUF’s own byte-level BPE metadata (no
external tokenizer);
[`rawToChar()`](https://rdrr.io/r/base/rawConversion.html) is the
caller’s interpretation. The same boundary serves byte-level, vision, or
pure-codec models later.

``` r

model <- rllm_gguf_model("SmolLM2-135M.Q4_K_M.gguf")
model
#> <rllm_model llama: 30 layers, n_embd 576, 9/3 heads, n_ff 1536,
#>  vocab 49152; 272 tensors (q5_0/f32/q4_k/q8_0/q6_k)>

gen <- rllm_generate(model, charToRaw(
    "The capital of France is Paris. The capital of Germany is"), n_new = 16L)
rawToChar(gen$raw)
#> [1] " Berlin. The capital of Italy is Rome. The capital of Spain is Madrid."
```

That transcript is a real recorded run (SmolLM2-135M `Q4_K_M`, 30
layers, grouped-query attention, a `q5_0`-dominant quantization mix):
~16 tokens/s CPU decode with the KV cache, 8.5x over cache-less
re-forwards, identical outputs.

What the engine does:

- [`rllm_gguf_model()`](https://sounkou-bioinfo.github.io/Rfmalloc/Rllm/reference/rllm_gguf_model.md)
  reads hyperparameters from GGUF metadata and imports 2-d weights
  **natively** (still quantized) into fmalloc-backed storage; the
  forward pass points GGML tensors at those payloads zero-copy.
- [`rllm_forward()`](https://sounkou-bioinfo.github.io/Rfmalloc/Rllm/reference/rllm_forward.md)
  assembles the GGML graph (RMSNorm, RoPE, causal self-attention with
  GQA, SwiGLU) and computes it on the GGML CPU backend through ’s
  C-callables — Rllm links no GGML itself.
- [`rllm_kv_cache()`](https://sounkou-bioinfo.github.io/Rfmalloc/Rllm/reference/rllm_kv_cache.md)
  allocates per-layer cache slabs, plain R memory or **fmalloc-backed**
  (the cache as a disk citizen: file-backed, evictable, and open to a
  quantized-cache codec later).
- [`rllm_generate()`](https://sounkou-bioinfo.github.io/Rfmalloc/Rllm/reference/rllm_generate.md)
  prefills the cache and decodes greedily one token per step, EOS-aware.

## Correctness discipline

Every layer is pinned to an independent reference:

- the forward pass matches a pure-R implementation of the same
  arithmetic to ~1e-6 relative on real-model weights (f32-twin
  roundtrip) and \< 1e-4 on hermetic synthetic models (MHA and GQA),
  with causality probes;
- incremental (KV-cache) logits equal whole-batch logits at every
  position;
- codec decoders are bit-identical to GGML’s type-traits `to_float`
  (`Rggml_dequantize`), the cross-validation that caught a real upstream
  gguf-tools Q4_K bug;
- `rllm_decode(rllm_encode(x))` is always `x`.

The hermetic tests write their synthetic GGUF models at test time with
’s own writer; an opt-in smoke test
(`RLLM_TEST_GGUF=<path to a llama-arch GGUF>`) exercises real files.

## Compute backends: today and roadmap

Today, inference runs on the **GGML CPU backend**:
runtime-SIMD-dispatched quantized kernels (AVX2 on x86, NEON on aarch64,
CPUID-selected at runtime — no non-portable flags recorded) with dense
products offloadable to R’s BLAS.

**CUDA is the next backend (roadmap — not implemented yet).** The plan
follows the same pattern as ’s BLAS backend: ’s `configure` will
**autodetect `nvcc`** — absent, nothing changes (CRAN/CI builds are
unaffected); present, GGML’s CUDA backend is compiled in and exposed as
a `Rggml_backend_cuda_init` C-callable, and Rllm gains a backend switch
for forward/generate. The target is validated: the identical model and
quantization decode at ~455 tokens/s (~28x this CPU stack) on an RTX
5050 via upstream GGML CUDA. Vulkan remains on the long-term roadmap for
non-NVIDIA GPUs.

## Install

From the [r-universe](https://sounkou-bioinfo.r-universe.dev):

``` r

install.packages("Rllm",
  repos = c("https://sounkou-bioinfo.r-universe.dev", getOption("repos")))
```

or via a GitHub subdir ref:
`pak::pak("sounkou-bioinfo/Rfmalloc/packages/Rllm")`. Unix (Linux/macOS)
only. GPL (\>= 2).
