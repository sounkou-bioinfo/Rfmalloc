/*
 * rgguf_codecs.c - register gguflib's quantized dequantizers as Rfmalloc
 * tensor codecs (Rfmalloc C API version 4).
 *
 * A codec decodes a flat, block-aligned element range of a raw payload into
 * doubles. gguflib's per-type dequantizers process whole blocks but stop
 * exactly at the requested weight count, so a partial final block is safe.
 * The float intermediate returned by gguf_tensor_to_float() is bounded by
 * Rfmalloc's decode panel size, not by the tensor size.
 */
#include <errno.h>
#include <stdlib.h>

#include "rgguf.h"

#include <Rfmalloc.h>

/* Block geometries for the registered quantized types, mirroring the
 * gguf_tensor_type_features table internal to the vendored gguflib.c. */
static int rgguf_codec_geometry(uint32_t type, uint32_t *items, uint32_t *bytes)
{
    switch (type) {
    case GGUF_TYPE_Q4_0: *items = 32;  *bytes = 18;  return 0;
    case GGUF_TYPE_Q4_1: *items = 32;  *bytes = 20;  return 0;
    case GGUF_TYPE_Q8_0: *items = 32;  *bytes = 34;  return 0;
    case GGUF_TYPE_Q2_K: *items = 256; *bytes = 84;  return 0;
    case GGUF_TYPE_Q4_K: *items = 256; *bytes = 144; return 0;
    case GGUF_TYPE_Q6_K: *items = 256; *bytes = 210; return 0;
    default: return -1;
    }
}

static int rgguf_codec_decode(uint32_t type, const void *payload,
                              R_xlen_t elem_offset, R_xlen_t n_elems,
                              double *out)
{
    uint32_t items_per_block, bytes_per_block;
    if (rgguf_codec_geometry(type, &items_per_block, &bytes_per_block) != 0 ||
        n_elems < 0 || (elem_offset % (R_xlen_t) items_per_block) != 0) {
        return -1;
    }
    if (n_elems == 0) {
        return 0;
    }

    gguf_tensor sub;
    memset(&sub, 0, sizeof(sub));
    sub.type = type;
    sub.num_weights = (uint64_t) n_elems;
    sub.ndim = 1;
    sub.dim[0] = (uint64_t) n_elems;
    sub.weights_data = (uint8_t *) payload +
        ((uint64_t) elem_offset / items_per_block) * bytes_per_block;
    sub.bsize = ((sub.num_weights + items_per_block - 1) /
                 items_per_block) * bytes_per_block;

    float *f = gguf_tensor_to_float(&sub);
    if (f == NULL) {
        return -1;
    }
    for (R_xlen_t i = 0; i < n_elems; i++) {
        out[i] = (double) f[i];
    }
    free(f);
    return 0;
}

static int rgguf_decode_q4_0(const void *payload, R_xlen_t off, R_xlen_t n, double *out)
{
    return rgguf_codec_decode(GGUF_TYPE_Q4_0, payload, off, n, out);
}

static int rgguf_decode_q4_1(const void *payload, R_xlen_t off, R_xlen_t n, double *out)
{
    return rgguf_codec_decode(GGUF_TYPE_Q4_1, payload, off, n, out);
}

static int rgguf_decode_q8_0(const void *payload, R_xlen_t off, R_xlen_t n, double *out)
{
    return rgguf_codec_decode(GGUF_TYPE_Q8_0, payload, off, n, out);
}

static int rgguf_decode_q2_k(const void *payload, R_xlen_t off, R_xlen_t n, double *out)
{
    return rgguf_codec_decode(GGUF_TYPE_Q2_K, payload, off, n, out);
}

static int rgguf_decode_q4_k(const void *payload, R_xlen_t off, R_xlen_t n, double *out)
{
    return rgguf_codec_decode(GGUF_TYPE_Q4_K, payload, off, n, out);
}

static int rgguf_decode_q6_k(const void *payload, R_xlen_t off, R_xlen_t n, double *out)
{
    return rgguf_codec_decode(GGUF_TYPE_Q6_K, payload, off, n, out);
}

void rgguf_register_fmalloc_codecs(void)
{
    if (Rfmalloc_api_version() < 4) {
        return;
    }
    Rfmalloc_register_tensor_codec("q4_0", 32, 18, rgguf_decode_q4_0);
    Rfmalloc_register_tensor_codec("q4_1", 32, 20, rgguf_decode_q4_1);
    Rfmalloc_register_tensor_codec("q8_0", 32, 34, rgguf_decode_q8_0);
    Rfmalloc_register_tensor_codec("q2_k", 256, 84, rgguf_decode_q2_k);
    Rfmalloc_register_tensor_codec("q4_k", 256, 144, rgguf_decode_q4_k);
    Rfmalloc_register_tensor_codec("q6_k", 256, 210, rgguf_decode_q6_k);
}

SEXP RC_gguf_register_codecs(void)
{
    rgguf_register_fmalloc_codecs();
    return R_NilValue;
}

/* Copy the raw (still-encoded) payload bytes of a tensor into a
 * caller-allocated raw vector, for native fmalloc_tensor imports. */
SEXP RC_gguf_tensor_fill_raw(SEXP ctx_sexp, SEXP name, SEXP dest)
{
    gguf_ctx *ctx = rgguf_get_ctx(ctx_sexp);
    if (TYPEOF(name) != STRSXP || XLENGTH(name) != 1) {
        Rf_error("name must be a single string");
    }
    if (TYPEOF(dest) != RAWSXP) {
        Rf_error("dest must be a raw vector");
    }

    gguf_tensor tensor;
    if (!rgguf_find_tensor(ctx, CHAR(STRING_ELT(name, 0)), &tensor)) {
        Rf_error("no such tensor: '%s'", CHAR(STRING_ELT(name, 0)));
    }
    if ((uint64_t) XLENGTH(dest) < tensor.bsize) {
        Rf_error("dest has %lld bytes but tensor payload needs %llu",
                 (long long) XLENGTH(dest), (unsigned long long) tensor.bsize);
    }
    memcpy(RAW(dest), tensor.weights_data, tensor.bsize);
    return dest;
}
