/* Decode with GGML's authoritative type traits into caller-owned fmalloc. */
#include <inttypes.h>

#include "rgguf.h"

SEXP RC_gguf_tensor_fill(SEXP ctx_sexp, SEXP name_sexp, SEXP dest)
{
    rgguf_ctx *ctx = rgguf_get_ctx(ctx_sexp);
    if (TYPEOF(name_sexp) != STRSXP || XLENGTH(name_sexp) != 1 ||
        STRING_ELT(name_sexp, 0) == NA_STRING)
        Rf_error("name must be a single non-missing string");
    const char *name = CHAR(STRING_ELT(name_sexp, 0));
    struct Rggml_gguf_tensor tensor;
    if (!rgguf_find_tensor(ctx, name, &tensor))
        Rf_error("no such tensor: '%s'", name);
    if (TYPEOF(dest) != REALSXP)
        Rf_error("destination object must be a numeric vector");
    if ((int64_t)XLENGTH(dest) != tensor.n_elements)
        Rf_error("destination length does not match tensor '%s'", name);
    const void *data = rgguf_tensor_data(ctx, &tensor);
    if (!data) Rf_error("tensor '%s' points outside the GGUF file", name);
    if (Rggml_dequantize_double_ptr()(tensor.type, data, REAL(dest),
                                      tensor.n_elements) != 0)
        Rf_error("GGML cannot decode tensor '%s' of type '%s'", name,
                 rgguf_type_name(tensor.type));
    return R_NilValue;
}

/* Borrow the tensor's exact on-disk bytes. The returned Rfmalloc storage view
 * keeps ctx_sexp reachable, so its read-only mapping outlives every tensor
 * view made from it. */
SEXP RC_gguf_tensor_view(SEXP ctx_sexp, SEXP name_sexp, SEXP runtime_sexp)
{
    rgguf_ctx *ctx = rgguf_get_ctx(ctx_sexp);
    if (TYPEOF(name_sexp) != STRSXP || XLENGTH(name_sexp) != 1 ||
        STRING_ELT(name_sexp, 0) == NA_STRING)
        Rf_error("name must be a single non-missing string");
    const char *name = CHAR(STRING_ELT(name_sexp, 0));
    struct Rggml_gguf_tensor tensor;
    if (!rgguf_find_tensor(ctx, name, &tensor))
        Rf_error("no such tensor: '%s'", name);
    const void *data = rgguf_tensor_data(ctx, &tensor);
    if (!data) Rf_error("tensor '%s' points outside the GGUF file", name);

    SEXP view = PROTECT(Rfmalloc_storage_view(
        ctx_sexp, runtime_sexp, data, tensor.nbytes));
    UNPROTECT(1);
    return view;
}
