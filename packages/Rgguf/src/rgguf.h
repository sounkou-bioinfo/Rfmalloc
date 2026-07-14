/* Internal Rgguf interface. GGUF parsing and writing belong to Rggml's
 * official gguf.cpp; Rgguf owns only the read-only file mapping and R surface. */
#ifndef RGGUF_H
#define RGGUF_H

#include <stddef.h>
#include <stdint.h>

#include <R.h>
#include <Rinternals.h>

#include <Rfmalloc.h>
#include <Rggml.h>

#ifdef __cplusplus
extern "C" {
#endif

enum rgguf_value_type {
    RGGUF_VALUE_UINT8 = 0,
    RGGUF_VALUE_INT8 = 1,
    RGGUF_VALUE_UINT16 = 2,
    RGGUF_VALUE_INT16 = 3,
    RGGUF_VALUE_UINT32 = 4,
    RGGUF_VALUE_INT32 = 5,
    RGGUF_VALUE_FLOAT32 = 6,
    RGGUF_VALUE_BOOL = 7,
    RGGUF_VALUE_STRING = 8,
    RGGUF_VALUE_UINT64 = 10,
    RGGUF_VALUE_INT64 = 11,
    RGGUF_VALUE_FLOAT64 = 12
};

typedef struct {
    struct Rggml_gguf_context *meta;
    int fd;
    void *mapping;
    size_t size;
    size_t data_offset;
} rgguf_ctx;

extern SEXP Rgguf_ctx_tag;
rgguf_ctx *rgguf_get_ctx(SEXP x);
int rgguf_find_tensor(rgguf_ctx *ctx, const char *name,
                      struct Rggml_gguf_tensor *out);
const void *rgguf_tensor_data(rgguf_ctx *ctx,
                              const struct Rggml_gguf_tensor *tensor);
const char *rgguf_type_name(enum ggml_type type);
int rgguf_type_is_supported(enum ggml_type type);
void rgguf_register_fmalloc_codecs(void);

SEXP RC_gguf_open(SEXP path);
SEXP RC_gguf_close(SEXP ctx);
SEXP RC_gguf_metadata(SEXP ctx);
SEXP RC_gguf_tensor_table(SEXP ctx);
SEXP RC_gguf_tensor_names(SEXP ctx);
SEXP RC_gguf_tensor_info(SEXP ctx, SEXP name);
SEXP RC_gguf_tensor_fill(SEXP ctx, SEXP name, SEXP dest);
SEXP RC_gguf_tensor_fill_raw(SEXP ctx, SEXP name, SEXP dest);
SEXP RC_gguf_tensor_view(SEXP ctx, SEXP name, SEXP runtime);
SEXP RC_gguf_write(SEXP path, SEXP tensor_values, SEXP metadata);
SEXP RC_gguf_register_codecs(void);

#ifdef __cplusplus
}
#endif

#endif
