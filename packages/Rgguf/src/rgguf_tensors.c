/*
 * rgguf_tensors.c - tensor directory listing and by-name lookup.
 *
 * gguflib's gguf_ctx is a forward-only cursor over the mmapped file: reading
 * tensor info requires first consuming every key-value pair, and finding a
 * tensor "by name" means scanning the tensor directory from the start. Both
 * are cheap relative to actually dequantizing tensor payloads, so every
 * entry point here just rewinds and rescans.
 */
#include <string.h>

#include "rgguf.h"

void rgguf_skip_metadata(gguf_ctx *ctx)
{
    gguf_rewind(ctx);
    gguf_key key;
    while (gguf_get_key(ctx, &key)) {
        gguf_do_with_value(ctx, key.type, key.val, NULL, 0, 0, NULL);
    }
}

int rgguf_find_tensor(gguf_ctx *ctx, const char *name, gguf_tensor *out)
{
    size_t namelen = strlen(name);
    rgguf_skip_metadata(ctx);
    gguf_tensor t;
    while (gguf_get_tensor(ctx, &t)) {
        if (t.namelen == namelen && memcmp(t.name, name, namelen) == 0) {
            *out = t;
            return 1;
        }
    }
    return 0;
}

SEXP RC_gguf_tensor_names(SEXP ctx_sexp)
{
    gguf_ctx *ctx = rgguf_get_ctx(ctx_sexp);
    rgguf_skip_metadata(ctx);

    R_xlen_t n = (R_xlen_t) ctx->header->tensor_count;
    SEXP names = PROTECT(Rf_allocVector(STRSXP, n));
    gguf_tensor t;
    R_xlen_t i = 0;
    while (gguf_get_tensor(ctx, &t)) {
        SET_STRING_ELT(names, i, Rf_mkCharLenCE(t.name, (int) t.namelen, CE_UTF8));
        i++;
    }
    UNPROTECT(1);
    return names;
}

SEXP RC_gguf_tensor_table(SEXP ctx_sexp)
{
    gguf_ctx *ctx = rgguf_get_ctx(ctx_sexp);
    rgguf_skip_metadata(ctx);

    R_xlen_t n = (R_xlen_t) ctx->header->tensor_count;
    SEXP name = PROTECT(Rf_allocVector(STRSXP, n));
    SEXP type = PROTECT(Rf_allocVector(STRSXP, n));
    SEXP n_dims = PROTECT(Rf_allocVector(INTSXP, n));
    SEXP dims = PROTECT(Rf_allocVector(VECSXP, n));
    SEXP n_elements = PROTECT(Rf_allocVector(REALSXP, n));
    SEXP nbytes = PROTECT(Rf_allocVector(REALSXP, n));
    SEXP offset = PROTECT(Rf_allocVector(REALSXP, n));

    gguf_tensor t;
    R_xlen_t i = 0;
    while (gguf_get_tensor(ctx, &t)) {
        SET_STRING_ELT(name, i, Rf_mkCharLenCE(t.name, (int) t.namelen, CE_UTF8));
        SET_STRING_ELT(type, i, Rf_mkChar(gguf_get_tensor_type_name(t.type)));
        INTEGER(n_dims)[i] = (int) t.ndim;

        SEXP tdims = Rf_allocVector(INTSXP, t.ndim);
        SET_VECTOR_ELT(dims, i, tdims);
        for (uint32_t j = 0; j < t.ndim; j++) {
            INTEGER(tdims)[j] = (int) t.dim[j];
        }

        REAL(n_elements)[i] = (double) t.num_weights;
        REAL(nbytes)[i] = (double) t.bsize;
        REAL(offset)[i] = (double) t.offset;
        i++;
    }

    const char *names[] = {"name", "type", "n_dims", "dims", "n_elements", "nbytes", "offset"};
    SEXP out = PROTECT(Rf_allocVector(VECSXP, 7));
    SET_VECTOR_ELT(out, 0, name);
    SET_VECTOR_ELT(out, 1, type);
    SET_VECTOR_ELT(out, 2, n_dims);
    SET_VECTOR_ELT(out, 3, dims);
    SET_VECTOR_ELT(out, 4, n_elements);
    SET_VECTOR_ELT(out, 5, nbytes);
    SET_VECTOR_ELT(out, 6, offset);

    SEXP out_names = PROTECT(Rf_allocVector(STRSXP, 7));
    for (int j = 0; j < 7; j++) {
        SET_STRING_ELT(out_names, j, Rf_mkChar(names[j]));
    }
    Rf_setAttrib(out, R_NamesSymbol, out_names);

    UNPROTECT(9);
    return out;
}

SEXP RC_gguf_tensor_info(SEXP ctx_sexp, SEXP name_sexp)
{
    gguf_ctx *ctx = rgguf_get_ctx(ctx_sexp);
    if (TYPEOF(name_sexp) != STRSXP || LENGTH(name_sexp) != 1) {
        Rf_error("name must be a single string");
    }
    const char *name = CHAR(STRING_ELT(name_sexp, 0));

    gguf_tensor t;
    if (!rgguf_find_tensor(ctx, name, &t)) {
        return R_NilValue;
    }

    SEXP dims = PROTECT(Rf_allocVector(INTSXP, t.ndim));
    for (uint32_t j = 0; j < t.ndim; j++) {
        INTEGER(dims)[j] = (int) t.dim[j];
    }

    SEXP out = PROTECT(Rf_allocVector(VECSXP, 5));
    SET_VECTOR_ELT(out, 0, dims);
    SET_VECTOR_ELT(out, 1, Rf_ScalarString(Rf_mkChar(gguf_get_tensor_type_name(t.type))));
    SET_VECTOR_ELT(out, 2, Rf_ScalarLogical(rgguf_type_is_supported(t.type)));
    SET_VECTOR_ELT(out, 3, Rf_ScalarReal((double) t.num_weights));
    SET_VECTOR_ELT(out, 4, Rf_ScalarReal((double) t.bsize));

    SEXP out_names = PROTECT(Rf_allocVector(STRSXP, 5));
    SET_STRING_ELT(out_names, 0, Rf_mkChar("dims"));
    SET_STRING_ELT(out_names, 1, Rf_mkChar("type"));
    SET_STRING_ELT(out_names, 2, Rf_mkChar("supported"));
    SET_STRING_ELT(out_names, 3, Rf_mkChar("n_elements"));
    SET_STRING_ELT(out_names, 4, Rf_mkChar("nbytes"));
    Rf_setAttrib(out, R_NamesSymbol, out_names);

    UNPROTECT(3);
    return out;
}
