/*
 * rgguf_write.c - minimal GGUF writer.
 *
 * Writes every tensor as GGUF_TYPE_F32 (the writer exists mainly so the
 * package can build small GGUF fixtures for its own round-trip tests, but it
 * is exported since it is also handy for producing test fixtures for other
 * GGUF consumers). Metadata values are restricted to single strings or
 * single numbers (written as FLOAT64), which is all gguf_write_tensors()
 * promises to support.
 */
#include <string.h>
#include <stdlib.h>

#include "rgguf.h"

static void rgguf_append_string_kv(gguf_ctx *ctx, const char *key, SEXP value)
{
    const char *str = CHAR(STRING_ELT(value, 0));
    uint64_t slen = (uint64_t) strlen(str);
    size_t bufsize = sizeof(uint64_t) + (size_t) slen;

    uint8_t *buf = malloc(bufsize);
    if (buf == NULL) {
        Rf_error("out of memory writing metadata key '%s'", key);
    }
    memcpy(buf, &slen, sizeof(uint64_t));
    memcpy(buf + sizeof(uint64_t), str, (size_t) slen);

    int ok = gguf_append_kv(ctx, key, strlen(key), GGUF_VALUE_TYPE_STRING, buf, bufsize);
    free(buf);
    if (!ok) {
        Rf_error("failed to write metadata key '%s'", key);
    }
}

static void rgguf_append_numeric_kv(gguf_ctx *ctx, const char *key, SEXP value)
{
    double v = Rf_asReal(value);
    if (!gguf_append_kv(ctx, key, strlen(key), GGUF_VALUE_TYPE_FLOAT64, &v, sizeof(double))) {
        Rf_error("failed to write metadata key '%s'", key);
    }
}

SEXP RC_gguf_write(SEXP path, SEXP tensor_values, SEXP metadata)
{
    if (TYPEOF(path) != STRSXP || LENGTH(path) != 1) {
        Rf_error("path must be a single string");
    }
    const char *filename = CHAR(STRING_ELT(path, 0));

    R_xlen_t ntensors = Rf_xlength(tensor_values);
    if (ntensors == 0) {
        Rf_error("tensors must be a non-empty named list");
    }
    SEXP tensor_names = Rf_getAttrib(tensor_values, R_NamesSymbol);
    if (tensor_names == R_NilValue) {
        Rf_error("tensors must be a named list");
    }

    gguf_ctx *out = gguf_create(filename, GGUF_OVERWRITE);
    if (out == NULL) {
        Rf_error("failed to create GGUF file '%s'", filename);
    }

    /* Metadata key-value pairs must all be appended before any tensor info
     * (gguflib rejects gguf_append_kv() once tensor_count > 0). */
    R_xlen_t nmeta = Rf_xlength(metadata);
    if (nmeta > 0) {
        SEXP meta_names = Rf_getAttrib(metadata, R_NamesSymbol);
        if (meta_names == R_NilValue) {
            gguf_close(out);
            Rf_error("metadata must be a named list");
        }
        for (R_xlen_t i = 0; i < nmeta; i++) {
            const char *key = CHAR(STRING_ELT(meta_names, i));
            SEXP value = VECTOR_ELT(metadata, i);
            if (TYPEOF(value) == STRSXP) {
                rgguf_append_string_kv(out, key, value);
            } else {
                rgguf_append_numeric_kv(out, key, value);
            }
        }
    }

    /* Pass 1: append tensor info entries, tracking the cumulative
     * (aligned) offset each tensor's data will land at, exactly like
     * gguf-tools.c's own writer does. */
    uint64_t *nweights_arr = (uint64_t *) malloc((size_t) ntensors * sizeof(uint64_t));
    if (nweights_arr == NULL) {
        gguf_close(out);
        Rf_error("out of memory");
    }

    uint64_t tensor_off = 0;
    for (R_xlen_t i = 0; i < ntensors; i++) {
        SEXP tv = VECTOR_ELT(tensor_values, i);
        const char *tname = CHAR(STRING_ELT(tensor_names, i));
        SEXP dim_attr = Rf_getAttrib(tv, R_DimSymbol);

        uint32_t ndim;
        uint64_t dims[GGUF_TENSOR_MAX_DIM];
        uint64_t nweights = 1;
        if (dim_attr == R_NilValue) {
            ndim = 1;
            dims[0] = (uint64_t) XLENGTH(tv);
            nweights = dims[0];
        } else {
            ndim = (uint32_t) LENGTH(dim_attr);
            if (ndim == 0 || ndim > GGUF_TENSOR_MAX_DIM) {
                free(nweights_arr);
                gguf_close(out);
                Rf_error("tensor '%s' has an unsupported number of dimensions", tname);
            }
            int *dp = INTEGER(dim_attr);
            for (uint32_t j = 0; j < ndim; j++) {
                dims[j] = (uint64_t) dp[j];
                nweights *= dims[j];
            }
        }
        if (nweights != (uint64_t) XLENGTH(tv)) {
            free(nweights_arr);
            gguf_close(out);
            Rf_error("tensor '%s' dim attribute does not match its length", tname);
        }
        nweights_arr[i] = nweights;

        tensor_off += gguf_get_alignment_padding(out->alignment, tensor_off);
        int ok = gguf_append_tensor_info(out, tname, strlen(tname), ndim, dims,
            GGUF_TYPE_F32, tensor_off);
        if (!ok) {
            free(nweights_arr);
            gguf_close(out);
            Rf_error("failed to write tensor info for '%s'", tname);
        }
        tensor_off += nweights * sizeof(float);
    }

    /* Pass 2: append tensor payloads in the same order, widening R's
     * doubles down to the float32 GGUF expects. */
    for (R_xlen_t i = 0; i < ntensors; i++) {
        SEXP tv = VECTOR_ELT(tensor_values, i);
        uint64_t nweights = nweights_arr[i];
        const double *src = REAL(tv);

        float *buf = (float *) malloc((size_t) nweights * sizeof(float));
        if (buf == NULL) {
            free(nweights_arr);
            gguf_close(out);
            Rf_error("out of memory writing tensor data");
        }
        for (uint64_t j = 0; j < nweights; j++) {
            buf[j] = (float) src[j];
        }
        int ok = gguf_append_tensor_data(out, buf, (uint64_t) nweights * sizeof(float));
        free(buf);
        if (!ok) {
            free(nweights_arr);
            gguf_close(out);
            Rf_error("failed to write tensor data for '%s'", CHAR(STRING_ELT(tensor_names, i)));
        }
    }

    free(nweights_arr);
    gguf_close(out);
    return R_NilValue;
}
