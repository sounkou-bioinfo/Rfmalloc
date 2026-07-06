/*
 * rgguf_init.c - native routine registration.
 *
 * Mirrors Rfmalloc's own approach: register every .Call entry point
 * explicitly and disable dynamic symbol lookup, so R CMD check does not
 * complain about unregistered native symbols.
 */
#include <R_ext/Rdynload.h>

#include "rgguf.h"

static const R_CallMethodDef CallEntries[] = {
    {"RC_gguf_open", (DL_FUNC) &RC_gguf_open, 1},
    {"RC_gguf_close", (DL_FUNC) &RC_gguf_close, 1},
    {"RC_gguf_metadata", (DL_FUNC) &RC_gguf_metadata, 1},
    {"RC_gguf_tensor_table", (DL_FUNC) &RC_gguf_tensor_table, 1},
    {"RC_gguf_tensor_names", (DL_FUNC) &RC_gguf_tensor_names, 1},
    {"RC_gguf_tensor_info", (DL_FUNC) &RC_gguf_tensor_info, 2},
    {"RC_gguf_tensor_fill", (DL_FUNC) &RC_gguf_tensor_fill, 3},
    {"RC_gguf_write", (DL_FUNC) &RC_gguf_write, 3},
    {NULL, NULL, 0}
};

void R_init_Rgguf(DllInfo *dll)
{
    Rgguf_ctx_tag = Rf_install("Rgguf.gguf_ctx");
    R_registerRoutines(dll, NULL, CallEntries, NULL, NULL);
    R_useDynamicSymbols(dll, FALSE);
}
