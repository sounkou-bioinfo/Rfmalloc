.onLoad <- function(libname, pkgname) {
    # The codecs bridge Rfmalloc storage to Rggml's official GGUF decoder.
    # Load both carrier DLLs before resolving their C-callables.
    loadNamespace("Rfmalloc")
    loadNamespace("Rggml")
    .Call("RC_gguf_register_codecs")
    invisible()
}
