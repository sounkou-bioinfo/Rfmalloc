#' Rllm: Native Quantized Matrix Products and LLM Inference over Rfmalloc
#'
#' Rllm is the composition layer of the Rfmalloc ecosystem. It registers
#' \pkg{Rggml} (a vendored GGML build with runtime-SIMD-dispatched quantized
#' kernels) as a codec-aware matrix-multiply backend for \pkg{Rfmalloc}, so
#' products of file-backed quantized tensors run natively in quantized space
#' rather than being decoded to double first. Together with \pkg{Rgguf} (which
#' exposes GGUF model weights as borrowed typed spans) this lets a
#' larger-than-memory model's linear layers multiply through GGML's
#' SIMD-accelerated dot kernels, zero-copy from the original GGUF mapping.
#'
#' Loading the package registers and selects the \code{"ggml"} backend; toggle
#' it with [rllm_use_ggml()]. Build quantized tensors with
#' [rllm_quantize_tensor()].
#'
#' @useDynLib Rllm, .registration = TRUE
#' @keywords internal
"_PACKAGE"
