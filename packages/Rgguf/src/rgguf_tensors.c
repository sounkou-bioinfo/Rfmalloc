/* Tensor directory queries over Rggml's indexed official GGUF context. */
#include <limits.h>
#include <stdint.h>

#include "rgguf.h"

const char *rgguf_type_name(enum ggml_type type)
{
    switch (type) {
    case GGML_TYPE_Q2_K: return "q2_k";
    case GGML_TYPE_Q3_K: return "q3_k";
    case GGML_TYPE_Q4_K: return "q4_k";
    case GGML_TYPE_Q5_K: return "q5_k";
    case GGML_TYPE_Q6_K: return "q6_k";
    case GGML_TYPE_Q8_K: return "q8_k";
    default: return Rggml_type_name_ptr()(type);
    }
}

int rgguf_type_is_supported(enum ggml_type type)
{
    return Rggml_can_dequantize_ptr()(type);
}

int rgguf_find_tensor(rgguf_ctx *ctx, const char *name,
                      struct Rggml_gguf_tensor *out)
{
    const int64_t id = Rggml_gguf_find_tensor_ptr()(ctx->meta, name);
    return id >= 0 && Rggml_gguf_tensor_ptr()(ctx->meta, id, out) == 0;
}

const void *rgguf_tensor_data(rgguf_ctx *ctx,
                              const struct Rggml_gguf_tensor *tensor)
{
    if (!ctx || !tensor || ctx->data_offset > ctx->size ||
        tensor->offset > ctx->size - ctx->data_offset ||
        tensor->nbytes > ctx->size - ctx->data_offset - tensor->offset)
        return NULL;
    return (const unsigned char *)ctx->mapping + ctx->data_offset + tensor->offset;
}

SEXP RC_gguf_tensor_names(SEXP ctx_sexp)
{
    rgguf_ctx *ctx = rgguf_get_ctx(ctx_sexp);
    const int64_t n64 = Rggml_gguf_n_tensors_ptr()(ctx->meta);
    if (n64 < 0 || (uint64_t)n64 > (uint64_t)R_XLEN_T_MAX)
        Rf_error("GGUF tensor table is too large for R");
    SEXP names = PROTECT(Rf_allocVector(STRSXP, (R_xlen_t)n64));
    for (int64_t i = 0; i < n64; ++i) {
        struct Rggml_gguf_tensor tensor;
        if (Rggml_gguf_tensor_ptr()(ctx->meta, i, &tensor) != 0)
            Rf_error("failed to read GGUF tensor entry %lld", (long long)i);
        SET_STRING_ELT(names, i, Rf_mkCharCE(tensor.name, CE_UTF8));
    }
    UNPROTECT(1);
    return names;
}

static SEXP rgguf_dims(const struct Rggml_gguf_tensor *tensor)
{
    SEXP dims = Rf_allocVector(INTSXP, tensor->n_dims);
    for (int j = 0; j < tensor->n_dims; ++j) {
        if (tensor->ne[j] < 0 || tensor->ne[j] > INT_MAX)
            Rf_error("tensor '%s' dimension is too large for R", tensor->name);
        INTEGER(dims)[j] = (int)tensor->ne[j];
    }
    return dims;
}

SEXP RC_gguf_tensor_table(SEXP ctx_sexp)
{
    rgguf_ctx *ctx = rgguf_get_ctx(ctx_sexp);
    const int64_t n64 = Rggml_gguf_n_tensors_ptr()(ctx->meta);
    if (n64 < 0 || (uint64_t)n64 > (uint64_t)R_XLEN_T_MAX)
        Rf_error("GGUF tensor table is too large for R");
    const R_xlen_t n = (R_xlen_t)n64;

    SEXP name = PROTECT(Rf_allocVector(STRSXP, n));
    SEXP type = PROTECT(Rf_allocVector(STRSXP, n));
    SEXP n_dims = PROTECT(Rf_allocVector(INTSXP, n));
    SEXP dims = PROTECT(Rf_allocVector(VECSXP, n));
    SEXP n_elements = PROTECT(Rf_allocVector(REALSXP, n));
    SEXP nbytes = PROTECT(Rf_allocVector(REALSXP, n));
    SEXP offset = PROTECT(Rf_allocVector(REALSXP, n));

    for (R_xlen_t i = 0; i < n; ++i) {
        struct Rggml_gguf_tensor tensor;
        if (Rggml_gguf_tensor_ptr()(ctx->meta, i, &tensor) != 0)
            Rf_error("failed to read GGUF tensor entry %lld", (long long)i);
        SET_STRING_ELT(name, i, Rf_mkCharCE(tensor.name, CE_UTF8));
        SET_STRING_ELT(type, i, Rf_mkChar(rgguf_type_name(tensor.type)));
        INTEGER(n_dims)[i] = tensor.n_dims;
        SEXP tdims = PROTECT(rgguf_dims(&tensor));
        SET_VECTOR_ELT(dims, i, tdims);
        UNPROTECT(1);
        REAL(n_elements)[i] = (double)tensor.n_elements;
        REAL(nbytes)[i] = (double)tensor.nbytes;
        REAL(offset)[i] = (double)(ctx->data_offset + tensor.offset);
    }

    SEXP out = PROTECT(Rf_allocVector(VECSXP, 7));
    SET_VECTOR_ELT(out, 0, name);
    SET_VECTOR_ELT(out, 1, type);
    SET_VECTOR_ELT(out, 2, n_dims);
    SET_VECTOR_ELT(out, 3, dims);
    SET_VECTOR_ELT(out, 4, n_elements);
    SET_VECTOR_ELT(out, 5, nbytes);
    SET_VECTOR_ELT(out, 6, offset);
    const char *colnames[] = {
        "name", "type", "n_dims", "dims", "n_elements", "nbytes", "offset"
    };
    SEXP out_names = PROTECT(Rf_allocVector(STRSXP, 7));
    for (int j = 0; j < 7; ++j)
        SET_STRING_ELT(out_names, j, Rf_mkChar(colnames[j]));
    Rf_setAttrib(out, R_NamesSymbol, out_names);
    UNPROTECT(9);
    return out;
}

SEXP RC_gguf_tensor_info(SEXP ctx_sexp, SEXP name_sexp)
{
    rgguf_ctx *ctx = rgguf_get_ctx(ctx_sexp);
    if (TYPEOF(name_sexp) != STRSXP || XLENGTH(name_sexp) != 1 ||
        STRING_ELT(name_sexp, 0) == NA_STRING)
        Rf_error("name must be a single non-missing string");
    struct Rggml_gguf_tensor tensor;
    if (!rgguf_find_tensor(ctx, CHAR(STRING_ELT(name_sexp, 0)), &tensor))
        return R_NilValue;

    SEXP dims = PROTECT(rgguf_dims(&tensor));
    SEXP out = PROTECT(Rf_allocVector(VECSXP, 5));
    SET_VECTOR_ELT(out, 0, dims);
    SET_VECTOR_ELT(out, 1, Rf_ScalarString(Rf_mkChar(rgguf_type_name(tensor.type))));
    SET_VECTOR_ELT(out, 2, Rf_ScalarLogical(rgguf_type_is_supported(tensor.type)));
    SET_VECTOR_ELT(out, 3, Rf_ScalarReal((double)tensor.n_elements));
    SET_VECTOR_ELT(out, 4, Rf_ScalarReal((double)tensor.nbytes));
    const char *names[] = {"dims", "type", "supported", "n_elements", "nbytes"};
    SEXP out_names = PROTECT(Rf_allocVector(STRSXP, 5));
    for (int j = 0; j < 5; ++j)
        SET_STRING_ELT(out_names, j, Rf_mkChar(names[j]));
    Rf_setAttrib(out, R_NamesSymbol, out_names);
    UNPROTECT(3);
    return out;
}
