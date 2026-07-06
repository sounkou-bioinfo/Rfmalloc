.onLoad <- function(libname, pkgname) {
    # Register gguflib's quantized dequantizers as Rfmalloc tensor codecs.
    # This runs here rather than in R_init_Rgguf() because Rfmalloc's
    # C-callables only exist once its DLL is loaded, and nothing else forces
    # that before Rgguf's own load: the NAMESPACE has no import() directives
    # (all cross-package calls use Rfmalloc::), so load it explicitly first.
    loadNamespace("Rfmalloc")
    .Call("RC_gguf_register_codecs")
    invisible()
}
