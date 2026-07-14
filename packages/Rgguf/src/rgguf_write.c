/* Minimal R fixture writer backed by GGML's official GGUF writer. */
#include <stdint.h>
#include <stdlib.h>

#include "rgguf.h"

SEXP RC_gguf_write(SEXP path, SEXP tensors, SEXP metadata)
{
    if (TYPEOF(path) != STRSXP || XLENGTH(path) != 1 ||
        STRING_ELT(path, 0) == NA_STRING)
        Rf_error("path must be a single non-missing string");
    if (TYPEOF(tensors) != VECSXP || XLENGTH(tensors) < 1)
        Rf_error("tensors must be a non-empty list");
    SEXP tensor_names = Rf_getAttrib(tensors, R_NamesSymbol);
    if (TYPEOF(tensor_names) != STRSXP ||
        XLENGTH(tensor_names) != XLENGTH(tensors))
        Rf_error("tensors must be named");
    for (R_xlen_t i = 0; i < XLENGTH(tensors); ++i)
        if (TYPEOF(VECTOR_ELT(tensors, i)) != REALSXP)
            Rf_error("tensor '%s' must be double",
                     CHAR(STRING_ELT(tensor_names, i)));

    struct Rggml_gguf_writer *writer = Rggml_gguf_writer_open_ptr()();
    if (!writer) Rf_error("failed to create GGML GGUF writer");

    SEXP meta_names = Rf_getAttrib(metadata, R_NamesSymbol);
    for (R_xlen_t i = 0; i < XLENGTH(metadata); ++i) {
        const char *key = CHAR(STRING_ELT(meta_names, i));
        SEXP value = VECTOR_ELT(metadata, i);
        int rc;
        if (TYPEOF(value) == STRSXP && XLENGTH(value) == 1) {
            rc = Rggml_gguf_writer_set_string_ptr()(
                writer, key, CHAR(STRING_ELT(value, 0)));
        } else if (TYPEOF(value) == STRSXP) {
            const R_xlen_t n = XLENGTH(value);
            const char **strings = (const char **)R_alloc(
                n ? (size_t)n : 1, sizeof(*strings));
            for (R_xlen_t j = 0; j < n; ++j)
                strings[j] = CHAR(STRING_ELT(value, j));
            rc = Rggml_gguf_writer_set_strings_ptr()(
                writer, key, strings, (size_t)n);
        } else {
            rc = Rggml_gguf_writer_set_f64_ptr()(
                writer, key, Rf_asReal(value));
        }
        if (rc != 0) {
            Rggml_gguf_writer_close_ptr()(writer);
            Rf_error("failed to write metadata key '%s'", key);
        }
    }

    for (R_xlen_t i = 0; i < XLENGTH(tensors); ++i) {
        SEXP value = VECTOR_ELT(tensors, i);
        SEXP dim = Rf_getAttrib(value, R_DimSymbol);
        int n_dims = dim == R_NilValue ? 1 : (int)XLENGTH(dim);
        if (n_dims < 1 || n_dims > GGML_MAX_DIMS) {
            Rggml_gguf_writer_close_ptr()(writer);
            Rf_error("tensor '%s' has too many dimensions",
                     CHAR(STRING_ELT(tensor_names, i)));
        }
        int64_t ne[GGML_MAX_DIMS];
        for (int j = 0; j < GGML_MAX_DIMS; ++j) ne[j] = 1;
        if (dim == R_NilValue) {
            ne[0] = XLENGTH(value);
        } else {
            for (int j = 0; j < n_dims; ++j) ne[j] = INTEGER(dim)[j];
        }
        if (Rggml_gguf_writer_add_f32_ptr()(
                writer, CHAR(STRING_ELT(tensor_names, i)), n_dims, ne,
                REAL(value)) != 0) {
            Rggml_gguf_writer_close_ptr()(writer);
            Rf_error("failed to add tensor '%s'",
                     CHAR(STRING_ELT(tensor_names, i)));
        }
    }

    if (Rggml_gguf_writer_write_ptr()(writer, CHAR(STRING_ELT(path, 0))) != 0) {
        Rggml_gguf_writer_close_ptr()(writer);
        Rf_error("failed to write GGUF file '%s'", CHAR(STRING_ELT(path, 0)));
    }
    Rggml_gguf_writer_close_ptr()(writer);
    return R_NilValue;
}
