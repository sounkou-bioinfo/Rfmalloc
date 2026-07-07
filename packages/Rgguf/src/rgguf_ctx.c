/*
 * rgguf_ctx.c - gguf_ctx external pointer lifecycle.
 *
 * gguf_open() mmaps the whole file (see gguflib.c), so the external pointer
 * we hand back to R owns that mapping until either gguf_close() is called
 * explicitly (via the internal .gguf_close() R helper) or the pointer is
 * garbage collected, whichever happens first.
 */
#include <stdlib.h>
#include <string.h>

#include "rgguf.h"

SEXP Rgguf_ctx_tag = NULL;

static void rgguf_finalize_ctx(SEXP ctx_sexp)
{
    gguf_ctx *ctx = (gguf_ctx *) R_ExternalPtrAddr(ctx_sexp);
    if (ctx != NULL) {
        gguf_close(ctx);
        R_ClearExternalPtr(ctx_sexp);
    }
}

gguf_ctx *rgguf_get_ctx(SEXP x)
{
    if (TYPEOF(x) != EXTPTRSXP || R_ExternalPtrTag(x) != Rgguf_ctx_tag) {
        Rf_error("not a valid 'gguf_ctx' object");
    }
    gguf_ctx *ctx = (gguf_ctx *) R_ExternalPtrAddr(x);
    if (ctx == NULL) {
        Rf_error("gguf context has already been closed");
    }
    return ctx;
}

SEXP RC_gguf_open(SEXP path)
{
    if (TYPEOF(path) != STRSXP || LENGTH(path) != 1) {
        Rf_error("path must be a single string");
    }
    const char *filename = CHAR(STRING_ELT(path, 0));

    gguf_ctx *ctx = gguf_open(filename);
    if (ctx == NULL) {
        Rf_error(
            "failed to open GGUF file '%s' (missing, unreadable, not "
            "writable, or not a valid GGUF file; note that the vendored "
            "gguflib parser mmaps files read/write, so the file must be "
            "writable even when only reading it)",
            filename);
    }

    SEXP ctx_sexp = PROTECT(R_MakeExternalPtr(ctx, Rgguf_ctx_tag, R_NilValue));
    R_RegisterCFinalizerEx(ctx_sexp, rgguf_finalize_ctx, TRUE);
    UNPROTECT(1);
    return ctx_sexp;
}

SEXP RC_gguf_close(SEXP ctx_sexp)
{
    if (TYPEOF(ctx_sexp) != EXTPTRSXP || R_ExternalPtrTag(ctx_sexp) != Rgguf_ctx_tag) {
        Rf_error("not a valid 'gguf_ctx' object");
    }
    gguf_ctx *ctx = (gguf_ctx *) R_ExternalPtrAddr(ctx_sexp);
    if (ctx != NULL) {
        gguf_close(ctx);
        R_ClearExternalPtr(ctx_sexp);
    }
    return R_NilValue;
}
