/*
 * rgguf_read.c - dequantize/widen a gguf_tensor directly into a
 * caller-allocated buffer of doubles.
 *
 * This is the "C side fills it" half of Rgguf's design: R allocates the
 * Rfmalloc-backed destination (create_fmalloc_matrix()/create_fmalloc_array())
 * and hands us its writable REAL() payload; we never allocate the
 * destination ourselves.
 */
#include <inttypes.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "rgguf.h"
#include "fp16.h"
#include "bf16.h"

int rgguf_type_is_supported(uint32_t type)
{
    switch (type) {
    case GGUF_TYPE_F32:
    case GGUF_TYPE_F16:
    case GGUF_TYPE_BF16:
    case GGUF_TYPE_F64:
    case GGUF_TYPE_I8:
    case GGUF_TYPE_I16:
    case GGUF_TYPE_I32:
    case GGUF_TYPE_I64:
    /* The following are the quantized formats gguflib's own
     * gguf_tensor_to_float() knows how to dequantize (see gguflib.c). */
    case GGUF_TYPE_Q8_0:
    case GGUF_TYPE_Q4_K:
    case GGUF_TYPE_Q6_K:
    case GGUF_TYPE_Q2_K:
    case GGUF_TYPE_Q4_0:
    case GGUF_TYPE_Q4_1:
        return 1;
    default:
        return 0;
    }
}

void rgguf_tensor_fill_double(gguf_tensor *tensor, double *dst)
{
    uint64_t n = tensor->num_weights;

    switch (tensor->type) {
    case GGUF_TYPE_F32: {
        const float *src = (const float *) tensor->weights_data;
        for (uint64_t i = 0; i < n; i++) dst[i] = (double) src[i];
        break;
    }
    case GGUF_TYPE_F16: {
        const uint16_t *src = (const uint16_t *) tensor->weights_data;
        for (uint64_t i = 0; i < n; i++) dst[i] = (double) from_half(src[i]);
        break;
    }
    case GGUF_TYPE_BF16: {
        const uint16_t *src = (const uint16_t *) tensor->weights_data;
        for (uint64_t i = 0; i < n; i++) dst[i] = (double) from_brain(src[i]);
        break;
    }
    case GGUF_TYPE_F64: {
        memcpy(dst, tensor->weights_data, (size_t) n * sizeof(double));
        break;
    }
    case GGUF_TYPE_I8: {
        const int8_t *src = (const int8_t *) tensor->weights_data;
        for (uint64_t i = 0; i < n; i++) dst[i] = (double) src[i];
        break;
    }
    case GGUF_TYPE_I16: {
        const int16_t *src = (const int16_t *) tensor->weights_data;
        for (uint64_t i = 0; i < n; i++) dst[i] = (double) src[i];
        break;
    }
    case GGUF_TYPE_I32: {
        const int32_t *src = (const int32_t *) tensor->weights_data;
        for (uint64_t i = 0; i < n; i++) dst[i] = (double) src[i];
        break;
    }
    case GGUF_TYPE_I64: {
        const int64_t *src = (const int64_t *) tensor->weights_data;
        for (uint64_t i = 0; i < n; i++) dst[i] = (double) src[i];
        break;
    }
    default: {
        /* Quantized block formats (Q8_0/Q4_K/Q6_K/Q2_K/Q4_0/Q4_1, the ones
         * rgguf_type_is_supported() allows through to here): gguflib's own
         * gguf_tensor_to_float() dequantizes a whole tensor into a malloc'd
         * float buffer. Known v1 cost: this is a transient extra
         * num_weights*4 bytes on top of the num_weights*8 byte Rfmalloc
         * destination, freed immediately after we widen it below. A future
         * version could dequantize block-by-block straight into `dst`
         * (like the direct cases above) to avoid the intermediate buffer. */
        float *buf = gguf_tensor_to_float(tensor);
        if (buf == NULL) {
            Rf_error(
                "tensor type '%s' cannot be dequantized by the vendored gguflib parser",
                gguf_get_tensor_type_name(tensor->type));
        }
        for (uint64_t i = 0; i < n; i++) dst[i] = (double) buf[i];
        free(buf);
        break;
    }
    }
}

SEXP RC_gguf_tensor_fill(SEXP ctx_sexp, SEXP name_sexp, SEXP dest)
{
    gguf_ctx *ctx = rgguf_get_ctx(ctx_sexp);
    if (TYPEOF(name_sexp) != STRSXP || LENGTH(name_sexp) != 1) {
        Rf_error("name must be a single string");
    }
    const char *name = CHAR(STRING_ELT(name_sexp, 0));

    gguf_tensor t;
    if (!rgguf_find_tensor(ctx, name, &t)) {
        Rf_error("no such tensor: '%s'", name);
    }
    if (!rgguf_type_is_supported(t.type)) {
        Rf_error(
            "tensor '%s' has type '%s' which cannot be dequantized by the vendored gguflib parser",
            name, gguf_get_tensor_type_name(t.type));
    }
    if (TYPEOF(dest) != REALSXP) {
        Rf_error("destination object must be a numeric (double) vector");
    }
    if ((uint64_t) XLENGTH(dest) != t.num_weights) {
        Rf_error(
            "destination length (%" PRIdMAX ") does not match tensor element count (%" PRIu64 ")",
            (intmax_t) XLENGTH(dest), t.num_weights);
    }

    rgguf_tensor_fill_double(&t, REAL(dest));
    return R_NilValue;
}
