/*
 * rgguf_metadata.c - expose GGUF key-value metadata as an R list.
 *
 * Scalar values map to the closest native R type; arrays of a supported
 * scalar type map to an R vector of that type. Anything gguflib itself
 * cannot represent cleanly (an out-of-range value type, or a nested array -
 * which the GGUF spec allows but no real-world GGUF file actually uses for
 * metadata) is reported as a plain NULL list element rather than an error,
 * per the package's "never fail metadata scanning over one odd key" design.
 */
#include <string.h>

#include "rgguf.h"

typedef struct {
    SEXP result;        /* allocated R vector, or NULL until first value seen */
    R_xlen_t idx;        /* next slot to fill */
    int is_array;        /* currently inside an array value? */
    int unsupported;     /* saw a type we can't represent, or nested arrays */
} rgguf_kv_collector;

/* Map a gguf_value_type to the R vector SEXPTYPE used to store it (scalar or
 * as an array element type). Returns -1 for types we do not represent.
 * (SEXPTYPE is unsigned, so the sentinel is returned as a plain int.) */
static int rgguf_sexptype_for_value_type(uint32_t type)
{
    switch (type) {
    case GGUF_VALUE_TYPE_UINT8:
    case GGUF_VALUE_TYPE_INT8:
    case GGUF_VALUE_TYPE_UINT16:
    case GGUF_VALUE_TYPE_INT16:
    case GGUF_VALUE_TYPE_INT32:
        return INTSXP;
    case GGUF_VALUE_TYPE_UINT32:
    case GGUF_VALUE_TYPE_FLOAT32:
    case GGUF_VALUE_TYPE_UINT64:
    case GGUF_VALUE_TYPE_INT64:
    case GGUF_VALUE_TYPE_FLOAT64:
        return REALSXP;
    case GGUF_VALUE_TYPE_BOOL:
        return LGLSXP;
    case GGUF_VALUE_TYPE_STRING:
        return STRSXP;
    default:
        return -1;
    }
}

static void rgguf_store_scalar(SEXP vec, R_xlen_t idx, uint32_t type, union gguf_value *val)
{
    switch (type) {
    case GGUF_VALUE_TYPE_UINT8:
        INTEGER(vec)[idx] = val->uint8;
        break;
    case GGUF_VALUE_TYPE_INT8:
        INTEGER(vec)[idx] = val->int8;
        break;
    case GGUF_VALUE_TYPE_UINT16:
        INTEGER(vec)[idx] = val->uint16;
        break;
    case GGUF_VALUE_TYPE_INT16:
        INTEGER(vec)[idx] = val->int16;
        break;
    case GGUF_VALUE_TYPE_INT32:
        INTEGER(vec)[idx] = val->int32;
        break;
    case GGUF_VALUE_TYPE_UINT32:
        REAL(vec)[idx] = (double) val->uint32;
        break;
    case GGUF_VALUE_TYPE_FLOAT32:
        REAL(vec)[idx] = (double) val->float32;
        break;
    case GGUF_VALUE_TYPE_UINT64:
        REAL(vec)[idx] = (double) val->uint64;
        break;
    case GGUF_VALUE_TYPE_INT64:
        REAL(vec)[idx] = (double) val->int64;
        break;
    case GGUF_VALUE_TYPE_FLOAT64:
        REAL(vec)[idx] = val->float64;
        break;
    case GGUF_VALUE_TYPE_BOOL:
        LOGICAL(vec)[idx] = (val->boolval == 0 || val->boolval == 1)
            ? (int) val->boolval : NA_LOGICAL;
        break;
    case GGUF_VALUE_TYPE_STRING:
        SET_STRING_ELT(vec, idx, Rf_mkCharLenCE(
            val->string.string, (int) val->string.len, CE_UTF8));
        break;
    default:
        break; /* unreachable: caller checks rgguf_sexptype_for_value_type() first */
    }
}

static void rgguf_kv_callback(void *privdata, uint32_t type, union gguf_value *val,
                              uint64_t in_array, uint64_t array_len)
{
    rgguf_kv_collector *col = privdata;
    (void) in_array;

    if (type == GGUF_VALUE_TYPE_ARRAY_START) {
        if (col->is_array || col->result != NULL) {
            /* Nested array: not used by any real GGUF file. Bail out. */
            col->unsupported = 1;
            return;
        }
        int elt_type = rgguf_sexptype_for_value_type(val->array.type);
        if (elt_type < 0) {
            col->unsupported = 1;
            return;
        }
        col->is_array = 1;
        col->result = Rf_allocVector((SEXPTYPE) elt_type, (R_xlen_t) array_len);
        R_PreserveObject(col->result);
        col->idx = 0;
        return;
    }
    if (type == GGUF_VALUE_TYPE_ARRAY_END) {
        return;
    }
    if (col->unsupported) {
        return;
    }

    if (!col->is_array && col->result == NULL) {
        int scalar_type = rgguf_sexptype_for_value_type(type);
        if (scalar_type < 0) {
            col->unsupported = 1;
            return;
        }
        col->result = Rf_allocVector((SEXPTYPE) scalar_type, 1);
        R_PreserveObject(col->result);
        col->idx = 0;
    }
    if (col->idx >= Rf_xlength(col->result)) {
        return; /* defensive: should not happen given array_len bookkeeping */
    }
    rgguf_store_scalar(col->result, col->idx, type, val);
    col->idx++;
}

SEXP RC_gguf_metadata(SEXP ctx_sexp)
{
    gguf_ctx *ctx = rgguf_get_ctx(ctx_sexp);
    gguf_rewind(ctx);

    R_xlen_t n = (R_xlen_t) ctx->header->metadata_kv_count;
    SEXP names = PROTECT(Rf_allocVector(STRSXP, n));
    SEXP values = PROTECT(Rf_allocVector(VECSXP, n));

    gguf_key key;
    R_xlen_t i = 0;
    while (gguf_get_key(ctx, &key)) {
        SET_STRING_ELT(names, i, Rf_mkCharLenCE(key.name, (int) key.namelen, CE_UTF8));

        rgguf_kv_collector col;
        memset(&col, 0, sizeof(col));
        gguf_do_with_value(ctx, key.type, key.val, &col, 0, 0, rgguf_kv_callback);

        if (col.result != NULL) {
            if (!col.unsupported) {
                SET_VECTOR_ELT(values, i, col.result);
            }
            R_ReleaseObject(col.result);
        }
        i++;
    }

    Rf_setAttrib(values, R_NamesSymbol, names);
    UNPROTECT(2);
    return values;
}
