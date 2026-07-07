.onLoad <- function(libname, pkgname) {
    # The ggml typed backend calls Rfmalloc's and Rggml's C-callables, and
    # building/selecting it needs the GGUF quantized codecs registered (Rgguf's
    # own .onLoad does that). The NAMESPACE has no import() directives - every
    # cross-package call uses ::, so nothing forces those DLLs to load first;
    # load them explicitly before touching their C-callables.
    loadNamespace("Rfmalloc")
    loadNamespace("Rggml")
    loadNamespace("Rgguf")
    .Call("RC_rllm_register_backend", PACKAGE = "Rllm")
    # Register GGML-backed Rfmalloc codecs for the GGUF quantized types
    # gguflib cannot decode (q5_0/q5_1/q3_k/q5_k) - decoded through GGML's
    # reference to_float, so consistent-by-construction with the compute path.
    .Call("RC_rllm_register_codecs", PACKAGE = "Rllm")
    # Select it so Rfmalloc's typed-tensor products use ggml. Rfmalloc-scoped
    # and reversible: rllm_use_ggml(FALSE) restores the BLAS path.
    Rfmalloc::fmalloc_matmul_backend("ggml")
    invisible()
}

.onAttach <- function(libname, pkgname) {
    packageStartupMessage(
        "Rllm: ggml quantized matmul backend registered and active for ",
        "Rfmalloc typed tensors (disable with rllm_use_ggml(FALSE))."
    )
}
