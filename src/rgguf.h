/*
 * rgguf.h - internal interface shared by Rgguf's native routines.
 *
 * Rgguf wraps the vendored gguflib parser (gguflib.c/.h, fp16.c/.h, bf16.h;
 * see inst/COPYRIGHTS) and hands dequantized tensor data to R via
 * Rfmalloc-backed destinations that R itself allocates. Native code never
 * allocates the destination vector: it only fills one that R created with
 * Rfmalloc::create_fmalloc_matrix()/create_fmalloc_array().
 */
#ifndef RGGUF_H
#define RGGUF_H

#include <R.h>
#include <Rinternals.h>

#include "gguflib.h"

#ifdef __cplusplus
extern "C" {
#endif

/* External pointer tag used to validate "gguf_ctx" SEXPs passed back from R.
 * Installed once in R_init_Rgguf(). */
extern SEXP Rgguf_ctx_tag;

/* Unwrap and validate a gguf_ctx external pointer coming from R. Errors out
 * (via Rf_error, does not return) if x is not a live gguf_ctx pointer. */
gguf_ctx *rgguf_get_ctx(SEXP x);

/* Scan ctx from the start and locate a tensor by name. Returns 1 and fills
 * *out on success, 0 if no tensor with that name exists. As a side effect,
 * this rewinds and re-scans the whole context, since gguflib's parser is a
 * forward-only cursor over the mmapped file. */
int rgguf_find_tensor(gguf_ctx *ctx, const char *name, gguf_tensor *out);

/* Skip over the metadata key-value section so that the ctx cursor is
 * positioned at (or through) the tensor info section. */
void rgguf_skip_metadata(gguf_ctx *ctx);

/* Return 1 if rgguf_tensor_fill_double() below knows how to dequantize this
 * gguf tensor type, 0 otherwise (e.g. quantized formats gguflib itself does
 * not implement a dequantizer for). */
int rgguf_type_is_supported(uint32_t type);

/* Dequantize/widen tensor into a pre-allocated destination of
 * tensor->num_weights doubles. dst is expected to be the writable payload of
 * an Rfmalloc-backed REALSXP that R already allocated. Errors via Rf_error()
 * if the type is unsupported (callers should check rgguf_type_is_supported()
 * first so R can avoid allocating the destination at all). */
void rgguf_tensor_fill_double(gguf_tensor *tensor, double *dst);

/* .Call entry points, registered in rgguf_init.c. */
SEXP RC_gguf_open(SEXP path);
SEXP RC_gguf_close(SEXP ctx);
SEXP RC_gguf_metadata(SEXP ctx);
SEXP RC_gguf_tensor_table(SEXP ctx);
SEXP RC_gguf_tensor_names(SEXP ctx);
SEXP RC_gguf_tensor_info(SEXP ctx, SEXP name);
SEXP RC_gguf_tensor_fill(SEXP ctx, SEXP name, SEXP dest);
SEXP RC_gguf_write(SEXP path, SEXP tensor_values, SEXP metadata);

#ifdef __cplusplus
}
#endif

#endif /* RGGUF_H */
