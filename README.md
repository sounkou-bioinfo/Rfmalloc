
# Rfmalloc — out-of-core arrays for R

<!-- badges: start -->

[![R-CMD-check](https://github.com/sounkou-bioinfo/Rfmalloc/actions/workflows/R-CMD-check.yaml/badge.svg)](https://github.com/sounkou-bioinfo/Rfmalloc/actions/workflows/R-CMD-check.yaml)
<!-- badges: end -->

**One repo, one story: array computation on data that does not fit in
RAM, over pluggable storage codecs and compute backends.**

Memory-mapped file storage (a patched
[fmalloc](https://github.com/yasukata/fmalloc) allocator behind R’s
ALTREP) turns RAM from a hard cutoff into a spectrum of speed levels. On
top of that substrate sit two plugin registries that everything else
closes over:

- a **codec registry** — how bytes are stored: lossless (`f64` `f32`
  `f16` `bf16` `alp` `sparse`) for scientific data where bit-exactness
  is non-negotiable, and lossy GGUF quantized blocks (`q4_0` … `q6_k`)
  for model weights at 2–8 bits each;
- a **matmul backend registry** — how products are computed: R’s BLAS by
  default, [GGML](https://github.com/ggml-org/ggml) natively in
  quantized space, GPU backends next.

The contract that makes it compose is the **decline protocol**: any
backend may refuse any product, and the substrate falls back to bounded
decode panels + BLAS. Plugins change how fast you get an answer, never
whether it is correct.

## Two domains, one substrate

The same file-backed matrices and the same backend registry serve both
larger-than-RAM **scientific computing** and quantized **LLM inference**
— the whole point of the abstraction. Both examples below are
`%*%`/`crossprod` over fmalloc tensors; the only thing that differs is
which codec and backend the registry dispatches to.

### Genomics: out-of-core PCA (runs at render time)

A cells × genes expression matrix lives in a memory-mapped file, so it
scales past RAM (products stream in panels). Highly-variable-gene
selection and PCA never materialize an ordinary R copy, and the linear
algebra goes through the backend registry:

``` r
library(Rfmalloc)

rt <- open_fmalloc(tempfile(fileext = ".bin"), size_gb = 1)
set.seed(1)
# 3000 cells x 600 genes with 4 latent "cell-type" factors + noise; file-backed
factors  <- matrix(rnorm(3000 * 4), 3000, 4)          # per-cell factor scores
loadings <- matrix(rnorm(4 * 600), 4, 600)            # per-gene factor loadings
X <- create_fmalloc_matrix("numeric", nrow = 3000, ncol = 600, runtime = rt)
X[] <- 3 * (factors %*% loadings) + matrix(rnorm(3000 * 600), 3000, 600)

vars <- fmalloc_colVars(X)          # per-gene variance, single-pass reduction (no R copy)
length(which(vars > quantile(vars, 0.9)))   # candidate highly-variable genes
#> [1] 60

pca <- fmalloc_pca(X, k = 6)        # Gram matrix + scores, dispatched through the backend registry
round(pca$sdev, 2)                  # 4 factors stand out above the noise floor
#> [1] 77.74 74.67 72.43 66.61  1.44  1.44
dim(pca$x)                          # cell scores in the top PC space
#> [1] 3000    6
```

### LLM: bytes in, bytes out

A GGUF model’s weights are memory-mapped and stay quantized; the
transformer runs through GGML’s SIMD kernels with a KV cache, and the
I/O boundary is raw bytes (the tokenizer is an edge codec built from
GGUF metadata — text is the caller’s interpretation, `rawToChar()`):

``` r
library(Rllm)
model <- rllm_gguf_model("SmolLM2-135M.Q4_K_M.gguf")   # weights mmap'd, still q4_k/q5_0/...
gen <- rllm_generate(model, charToRaw(
    "The capital of France is Paris. The capital of Germany is"), n_new = 16L)
rawToChar(gen$raw)
#> [1] " Berlin. The capital of Italy is Rome. The capital of Spain is Madrid."
```

That is a real recorded run (SmolLM2-135M `Q4_K_M`, 30 layers,
grouped-query attention): ~16 tokens/s CPU decode with the KV cache,
8.5× over cache-less re-forwards, and — checked against a pure-R
reference forward — numerically exact to the quantization.

## Compute backends: today and next

Because both PCA and inference dispatch their products through the
**one** backend registry, a faster backend accelerates both at once,
with no change to `fmalloc_pca()` or `rllm_generate()`:

- **today, CPU** — R’s BLAS for dense products; GGML’s
  runtime-SIMD-dispatched quantized kernels (AVX2 on x86, NEON on
  aarch64, CPUID-selected — no non-portable flags) for quantized
  weights; both out-of-core.
- **today, GPU: Vulkan.** GGML’s Vulkan backend is vendored and builds
  into `Rggml` **on request** —
  `install.packages("Rggml", configure.args =   "--with-vulkan")`,
  needing `glslc` and the Vulkan headers. It is not auto-detected,
  because GGML embeds 156 compiled SPIR-V shaders and the largest is a
  141 MB array literal costing ~5 GB of RAM to compile. It runs on any
  vendor’s GPU (NVIDIA, AMD, Intel). `rggml_vulkan_info()` reports what
  it sees; a build without it reports zero devices rather than failing,
  so callers can probe and fall back. Correctness is pinned against the
  CPU backend, and can be exercised without a GPU at all through Mesa’s
  software driver (`GGML_VK_ALLOW_CPU=1`).
- **next: CUDA.** NVIDIA-only, and the fastest: the same SmolLM2
  `Q4_K_M` decodes at **~455 tok/s (~28× this CPU stack)** on an RTX
  5050 via upstream GGML CUDA. The device-buffer residency API the
  Vulkan backend needed
  (`Rggml_backend_alloc_ctx_tensors`/`tensor_set`/`tensor_get`) is
  backend agnostic, so CUDA reuses it; what remains is vendoring
  `ggml-cuda` at a version matching the core and adding `nvcc` rules to
  the build.

## The packages

Four packages, one stack. Each links to its reference documentation; the
sources live under
[`packages/`](https://github.com/sounkou-bioinfo/Rfmalloc/tree/main/packages)
in this repository.

| package                                                              | role                                                                                                                                                                                        |
|----------------------------------------------------------------------|---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| [**Rfmalloc**](https://sounkou-bioinfo.github.io/Rfmalloc/Rfmalloc/) | the substrate: file-backed ALTREP vectors/matrices/tensors, codec + backend registries, panel matmul, out-of-core eviction, `fmalloc_pca()`/`fmalloc_colVars()` for genomics-scale matrices |
| [**Rgguf**](https://sounkou-bioinfo.github.io/Rfmalloc/Rgguf/)       | GGUF model files: metadata, tensor directory, dequantized or **native (still-quantized)** imports into fmalloc storage                                                                      |
| [**Rggml**](https://sounkou-bioinfo.github.io/Rfmalloc/Rggml/)       | compute carrier: vendored GGML with a BLAS backend (R’s own BLAS via a `cblas`→Fortran shim) and runtime-SIMD-dispatched quantized kernels                                                  |
| [**Rllm**](https://sounkou-bioinfo.github.io/Rfmalloc/Rllm/)         | composition + inference: registers Rggml as Rfmalloc’s codec-aware matmul backend, and builds the llama forward pass, KV cache, and byte-level generation                                   |

## Install

From the [r-universe](https://sounkou-bioinfo.r-universe.dev):

``` r
install.packages("Rllm",
  repos = c("https://sounkou-bioinfo.r-universe.dev", getOption("repos")))
```

or a GitHub subdir ref:
`pak::pak("sounkou-bioinfo/Rfmalloc/packages/Rllm")`. Unix (Linux/macOS)
only, except `Rfmalloc` itself, which also builds on Windows.

## Correctness discipline

Every quantized codec decoder is pinned **bit-identical** to GGML’s
reference (`Rggml_dequantize`, GGML’s own type-traits `to_float`) by
regression fixtures in Rgguf and live cross-validation in Rllm — the
discipline that caught a real upstream Q4_K dequantization bug in
gguf-tools (~33% wrong decodes of the most common real-model format;
fixed in our vendored copy). The inference graph is pinned to a pure-R
reference forward (~1e-6 on real weights); incremental KV-cache logits
equal whole-batch logits at every position; lossless codecs round-trip
exactly. The integration CI job installs the whole stack and runs every
package’s test suite against it (`tests/integration.R`).

## Layout & provenance

    packages/    the four R packages (independent installable units)
    tests/       cross-package integration entry points (run in CI)
    tools/       fixture generators and shared scripts

The vendored GGML is currently **v0.9.5** (via the CRAN-facing
[`ggmlR`](https://github.com/Zabis13/ggmlR), which supplies the
stdio/abort compliance shim; upstream standalone GGML is at v0.9.11). It
is refreshed deliberately, not continuously: the next refresh is folded
into the CUDA work and tested on real hardware, since the CUDA sources
must version-match the core. See `packages/Rggml/inst/COPYRIGHTS` for
the full version & update policy.

Related but deliberately separate:
[RsimdDispatch](https://github.com/sounkou-bioinfo/RsimdDispatch) (the
runtime-SIMD staging pattern Rggml adopts).

## License

GPL (\>= 2). Vendored third-party components (fmalloc, gguflib, GGML via
ggmlR) keep their upstream licenses; see each package’s
`inst/COPYRIGHTS`.
