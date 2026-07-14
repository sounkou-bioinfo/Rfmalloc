# Rllm: Native Quantized Matrix Products and LLM Inference over Rfmalloc

Rllm is the composition layer of the Rfmalloc ecosystem. It registers
Rggml (a vendored GGML build with runtime-SIMD-dispatched quantized
kernels) as a codec-aware matrix-multiply backend for Rfmalloc, so
products of file-backed quantized tensors run natively in quantized
space rather than being decoded to double first. Together with Rgguf
(which exposes GGUF model weights as borrowed typed spans) this lets a
larger-than-memory model's linear layers multiply through GGML's
SIMD-accelerated dot kernels, zero-copy from the original GGUF mapping.

## Details

Loading the package registers and selects the `"ggml"` backend; toggle
it with
[`rllm_use_ggml()`](https://sounkou-bioinfo.github.io/Rfmalloc/Rllm/reference/rllm_use_ggml.md).
Build quantized tensors with
[`rllm_quantize_tensor()`](https://sounkou-bioinfo.github.io/Rfmalloc/Rllm/reference/rllm_quantize_tensor.md).

## See also

Useful links:

- <https://github.com/sounkou-bioinfo/Rfmalloc>

- <https://sounkou-bioinfo.github.io/Rfmalloc/Rllm/>

- Report bugs at <https://github.com/sounkou-bioinfo/Rfmalloc/issues>

## Author

**Maintainer**: Sounkou Mahamane Toure <sounkoutoure@gmail.com>
