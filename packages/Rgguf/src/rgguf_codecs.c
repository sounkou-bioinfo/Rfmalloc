/* GGML-backed codecs for the common GGUF weight formats. */
#include <stdint.h>
#include <string.h>

#include "rgguf.h"

#include <Rfmalloc.h>

static ggml_backend_t rgguf_cpu;

static int rgguf_decode(enum ggml_type type, const void *payload,
                         R_xlen_t elem_offset, R_xlen_t n_elems, double *out)
{
    if (!payload || !out || elem_offset < 0 || n_elems < 0) return -1;
    if (n_elems == 0) return 0;
    const int64_t block = Rggml_blck_size_ptr()(type);
    if (block < 1 || elem_offset % block || n_elems % block) return -1;
    const size_t block_bytes = Rggml_row_size_ptr()(type, block);
    const unsigned char *src = (const unsigned char *)payload +
        (size_t)(elem_offset / block) * block_bytes;
    return Rggml_dequantize_double_ptr()(type, src, out, n_elems);
}

#define RGGUF_DECODER(name, type) \
    static int name(const void *p, R_xlen_t o, R_xlen_t n, double *d) \
    { return rgguf_decode(type, p, o, n, d); }
RGGUF_DECODER(rgguf_q4_0, GGML_TYPE_Q4_0)
RGGUF_DECODER(rgguf_q4_1, GGML_TYPE_Q4_1)
RGGUF_DECODER(rgguf_q5_0, GGML_TYPE_Q5_0)
RGGUF_DECODER(rgguf_q5_1, GGML_TYPE_Q5_1)
RGGUF_DECODER(rgguf_q8_0, GGML_TYPE_Q8_0)
RGGUF_DECODER(rgguf_q2_k, GGML_TYPE_Q2_K)
RGGUF_DECODER(rgguf_q3_k, GGML_TYPE_Q3_K)
RGGUF_DECODER(rgguf_q4_k, GGML_TYPE_Q4_K)
RGGUF_DECODER(rgguf_q5_k, GGML_TYPE_Q5_K)
RGGUF_DECODER(rgguf_q6_k, GGML_TYPE_Q6_K)
#undef RGGUF_DECODER

static void rgguf_register_one(const char *name, enum ggml_type type,
                                Rfmalloc_tensor_decode_fn decode)
{
    const int64_t block = Rggml_blck_size_ptr()(type);
    const size_t bytes = Rggml_row_size_ptr()(type, block);
    Rfmalloc_register_tensor_codec(name, (unsigned int)block,
                                   (unsigned int)bytes, decode);
}

void rgguf_register_fmalloc_codecs(void)
{
    if (!rgguf_cpu) rgguf_cpu = Rggml_backend_cpu_init_ptr()();
    if (!rgguf_cpu) Rf_error("failed to initialize GGML's CPU decoder");
    rgguf_register_one("q4_0", GGML_TYPE_Q4_0, rgguf_q4_0);
    rgguf_register_one("q4_1", GGML_TYPE_Q4_1, rgguf_q4_1);
    rgguf_register_one("q5_0", GGML_TYPE_Q5_0, rgguf_q5_0);
    rgguf_register_one("q5_1", GGML_TYPE_Q5_1, rgguf_q5_1);
    rgguf_register_one("q8_0", GGML_TYPE_Q8_0, rgguf_q8_0);
    rgguf_register_one("q2_k", GGML_TYPE_Q2_K, rgguf_q2_k);
    rgguf_register_one("q3_k", GGML_TYPE_Q3_K, rgguf_q3_k);
    rgguf_register_one("q4_k", GGML_TYPE_Q4_K, rgguf_q4_k);
    rgguf_register_one("q5_k", GGML_TYPE_Q5_K, rgguf_q5_k);
    rgguf_register_one("q6_k", GGML_TYPE_Q6_K, rgguf_q6_k);
}

SEXP RC_gguf_register_codecs(void)
{
    rgguf_register_fmalloc_codecs();
    return R_NilValue;
}

SEXP RC_gguf_tensor_fill_raw(SEXP ctx_sexp, SEXP name_sexp, SEXP dest)
{
    rgguf_ctx *ctx = rgguf_get_ctx(ctx_sexp);
    if (TYPEOF(name_sexp) != STRSXP || XLENGTH(name_sexp) != 1 ||
        STRING_ELT(name_sexp, 0) == NA_STRING)
        Rf_error("name must be a single non-missing string");
    if (TYPEOF(dest) != RAWSXP) Rf_error("dest must be a raw vector");
    const char *name = CHAR(STRING_ELT(name_sexp, 0));
    struct Rggml_gguf_tensor tensor;
    if (!rgguf_find_tensor(ctx, name, &tensor))
        Rf_error("no such tensor: '%s'", name);
    if ((size_t)XLENGTH(dest) < tensor.nbytes)
        Rf_error("destination is too short for tensor '%s'", name);
    const void *data = rgguf_tensor_data(ctx, &tensor);
    if (!data) Rf_error("tensor '%s' points outside the GGUF file", name);
    memcpy(RAW(dest), data, tensor.nbytes);
    return dest;
}
