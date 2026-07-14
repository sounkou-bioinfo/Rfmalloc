/* Expose metadata already parsed and validated by GGML's official reader. */
#include <stdint.h>

#include "rgguf.h"

static SEXPTYPE rgguf_value_sexptype(int type)
{
    switch (type) {
    case RGGUF_VALUE_UINT8:
    case RGGUF_VALUE_INT8:
    case RGGUF_VALUE_UINT16:
    case RGGUF_VALUE_INT16:
    case RGGUF_VALUE_INT32:
        return INTSXP;
    case RGGUF_VALUE_UINT32:
    case RGGUF_VALUE_FLOAT32:
    case RGGUF_VALUE_UINT64:
    case RGGUF_VALUE_INT64:
    case RGGUF_VALUE_FLOAT64:
        return REALSXP;
    case RGGUF_VALUE_BOOL:
        return LGLSXP;
    case RGGUF_VALUE_STRING:
        return STRSXP;
    default:
        return NILSXP;
    }
}

static void rgguf_copy_values(SEXP out, const struct Rggml_gguf_kv *kv,
                               int64_t id, rgguf_ctx *ctx)
{
    size_t i;
    switch (kv->type) {
    case RGGUF_VALUE_UINT8:
        for (i = 0; i < kv->n; ++i) INTEGER(out)[i] = ((const uint8_t *)kv->data)[i];
        break;
    case RGGUF_VALUE_INT8:
        for (i = 0; i < kv->n; ++i) INTEGER(out)[i] = ((const int8_t *)kv->data)[i];
        break;
    case RGGUF_VALUE_UINT16:
        for (i = 0; i < kv->n; ++i) INTEGER(out)[i] = ((const uint16_t *)kv->data)[i];
        break;
    case RGGUF_VALUE_INT16:
        for (i = 0; i < kv->n; ++i) INTEGER(out)[i] = ((const int16_t *)kv->data)[i];
        break;
    case RGGUF_VALUE_UINT32:
        for (i = 0; i < kv->n; ++i) REAL(out)[i] = ((const uint32_t *)kv->data)[i];
        break;
    case RGGUF_VALUE_INT32:
        for (i = 0; i < kv->n; ++i) INTEGER(out)[i] = ((const int32_t *)kv->data)[i];
        break;
    case RGGUF_VALUE_FLOAT32:
        for (i = 0; i < kv->n; ++i) REAL(out)[i] = ((const float *)kv->data)[i];
        break;
    case RGGUF_VALUE_BOOL:
        for (i = 0; i < kv->n; ++i) LOGICAL(out)[i] = ((const uint8_t *)kv->data)[i] ? 1 : 0;
        break;
    case RGGUF_VALUE_STRING:
        for (i = 0; i < kv->n; ++i) {
            const char *s = Rggml_gguf_kv_string_ptr()(ctx->meta, id, i);
            SET_STRING_ELT(out, i, s ? Rf_mkCharCE(s, CE_UTF8) : NA_STRING);
        }
        break;
    case RGGUF_VALUE_UINT64:
        for (i = 0; i < kv->n; ++i) REAL(out)[i] = (double)((const uint64_t *)kv->data)[i];
        break;
    case RGGUF_VALUE_INT64:
        for (i = 0; i < kv->n; ++i) REAL(out)[i] = (double)((const int64_t *)kv->data)[i];
        break;
    case RGGUF_VALUE_FLOAT64:
        for (i = 0; i < kv->n; ++i) REAL(out)[i] = ((const double *)kv->data)[i];
        break;
    }
}

SEXP RC_gguf_metadata(SEXP ctx_sexp)
{
    rgguf_ctx *ctx = rgguf_get_ctx(ctx_sexp);
    const int64_t n64 = Rggml_gguf_n_kv_ptr()(ctx->meta);
    if (n64 < 0 || (uint64_t)n64 > (uint64_t)R_XLEN_T_MAX)
        Rf_error("GGUF metadata table is too large for R");
    const R_xlen_t n = (R_xlen_t)n64;

    SEXP names = PROTECT(Rf_allocVector(STRSXP, n));
    SEXP values = PROTECT(Rf_allocVector(VECSXP, n));
    for (R_xlen_t i = 0; i < n; ++i) {
        struct Rggml_gguf_kv kv;
        if (Rggml_gguf_kv_ptr()(ctx->meta, i, &kv) != 0)
            Rf_error("failed to read GGUF metadata entry %lld", (long long)i);
        if (kv.n > (size_t)R_XLEN_T_MAX)
            Rf_error("GGUF metadata entry '%s' is too large for R", kv.key);
        if (kv.type != RGGUF_VALUE_STRING && kv.n && !kv.data)
            Rf_error("GGUF metadata entry '%s' has no value data", kv.key);
        SET_STRING_ELT(names, i, Rf_mkCharCE(kv.key, CE_UTF8));
        const SEXPTYPE type = rgguf_value_sexptype(kv.type);
        if (type == NILSXP) continue;
        SEXP value = PROTECT(Rf_allocVector(type, (R_xlen_t)kv.n));
        rgguf_copy_values(value, &kv, i, ctx);
        SET_VECTOR_ELT(values, i, value);
        UNPROTECT(1);
    }
    Rf_setAttrib(values, R_NamesSymbol, names);
    UNPROTECT(2);
    return values;
}
