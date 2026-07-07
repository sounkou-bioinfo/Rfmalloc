#ifndef RFMALLOC_API_H
#define RFMALLOC_API_H

#include <R.h>
#include <Rinternals.h>
#include <R_ext/Rdynload.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Rfmalloc C-callable API, version 6.
 *
 * These functions are resolved with R_GetCCallable(). Packages should import
 * Rfmalloc at runtime before calling them, for example by listing Rfmalloc in
 * LinkingTo and Imports. Returned SEXP objects follow normal R API ownership:
 * protect them if allocating further R objects, and preserve them if they must
 * outlive the current call.
 */

typedef int (*Rfmalloc_api_version_fun)(void);
typedef SEXP (*Rfmalloc_default_runtime_fun)(void);
typedef SEXP (*Rfmalloc_open_runtime_fun)(const char *filepath, double size_gb,
                                          const char *mode);
typedef SEXP (*Rfmalloc_create_vector_fun)(SEXP runtime, int sexptype,
                                           R_xlen_t length);
typedef SEXP (*Rfmalloc_create_vector_ex_fun)(SEXP runtime, int sexptype,
                                              R_xlen_t length,
                                              int zero_initialize);
typedef void (*Rfmalloc_cleanup_runtime_fun)(SEXP runtime);
typedef SEXP (*Rfmalloc_set_default_runtime_fun)(SEXP runtime);
typedef SEXP (*Rfmalloc_list_allocations_fun)(SEXP runtime);
typedef SEXP (*Rfmalloc_is_runtime_fun)(SEXP runtime);
typedef SEXP (*Rfmalloc_is_fmalloc_vector_fun)(SEXP vector);
typedef SEXP (*Rfmalloc_runtime_of_vector_fun)(SEXP vector);
typedef SEXP (*Rfmalloc_runtime_info_fun)(SEXP runtime);
typedef SEXP (*Rfmalloc_vector_type_fun)(SEXP vector);
typedef SEXP (*Rfmalloc_vector_length_fun)(SEXP vector);
typedef SEXP (*Rfmalloc_vector_payload_ptr_fun)(SEXP vector);
typedef SEXP (*Rfmalloc_vector_info_fun)(SEXP vector);
typedef SEXP (*Rfmalloc_destroy_vector_fun)(SEXP vector, int unsafe);

/*
 * Tensor codec registration (API version 4). A codec decodes a flat,
 * block-aligned element range of a typed tensor payload into doubles.
 * Ranges start on a block boundary and cover a whole number of blocks,
 * except possibly the final range for a payload; the codec must write
 * exactly n_elems doubles.
 */
typedef int (*Rfmalloc_tensor_decode_fn)(const void *payload,
                                         R_xlen_t elem_offset,
                                         R_xlen_t n_elems, double *out);
typedef int (*Rfmalloc_register_tensor_codec_fun)(const char *name,
                                                  unsigned int items_per_block,
                                                  unsigned int bytes_per_block,
                                                  Rfmalloc_tensor_decode_fn decode);

/*
 * Pluggable matrix-multiply backend (API version 5). Register a GEMM kernel
 * and select it (from R via fmalloc_matmul_backend(name), or another package)
 * so Rfmalloc's matrix products dispatch to it instead of R's BLAS. The
 * kernel computes C = alpha op(A) op(B) + beta C (op = 'N'/'T'), returning 0
 * if it handled the call or non-zero to decline (fall back to BLAS). Names
 * "blas" and the empty string are reserved.
 */
typedef int (*Rfmalloc_gemm_fn)(const char *transa, const char *transb,
                                int m, int n, int k, double alpha,
                                const double *A, int lda,
                                const double *B, int ldb,
                                double beta, double *C, int ldc);
typedef int (*Rfmalloc_register_matmul_backend_fun)(const char *name,
                                                    Rfmalloc_gemm_fn fn);

/*
 * Codec-aware (typed) backend hook (API version 6). Multiply a compressed
 * tensor by a dense double operand WITHOUT Rfmalloc decoding it to f64 first:
 * the backend receives the raw codec payload (byte-identical to how the codec
 * stored it — e.g. an fmalloc-mmap'd q4_k payload is a valid ggml Q4_K tensor)
 * and the dims, so a device/quantized engine can do native quantized matmul.
 * C = T x D when typed_on_left, else C = D x T. Return 0 if handled, non-zero
 * to decline (Rfmalloc then decodes panels and uses the dense gemm path).
 * Register with Rfmalloc_register_matmul_backend_ex (either fn may be NULL).
 */
typedef int (*Rfmalloc_typed_gemm_fn)(const char *codec,
                                      const void *payload, size_t payload_bytes,
                                      int tensor_nrow, int tensor_ncol,
                                      int typed_on_left,
                                      const double *dense, int dense_nrow, int dense_ncol,
                                      double *C);
typedef int (*Rfmalloc_register_matmul_backend_ex_fun)(const char *name,
                                                       Rfmalloc_gemm_fn fn,
                                                       Rfmalloc_typed_gemm_fn typed_fn);

static inline Rfmalloc_api_version_fun Rfmalloc_api_version_ptr(void)
{
    return (Rfmalloc_api_version_fun) R_GetCCallable("Rfmalloc", "Rfmalloc_api_version");
}

static inline Rfmalloc_default_runtime_fun Rfmalloc_default_runtime_ptr(void)
{
    return (Rfmalloc_default_runtime_fun) R_GetCCallable("Rfmalloc", "Rfmalloc_default_runtime");
}

static inline Rfmalloc_open_runtime_fun Rfmalloc_open_runtime_ptr(void)
{
    return (Rfmalloc_open_runtime_fun) R_GetCCallable("Rfmalloc", "Rfmalloc_open_runtime");
}

static inline Rfmalloc_create_vector_fun Rfmalloc_create_vector_ptr(void)
{
    return (Rfmalloc_create_vector_fun) R_GetCCallable("Rfmalloc", "Rfmalloc_create_vector");
}

static inline Rfmalloc_create_vector_ex_fun Rfmalloc_create_vector_ex_ptr(void)
{
    return (Rfmalloc_create_vector_ex_fun) R_GetCCallable("Rfmalloc", "Rfmalloc_create_vector_ex");
}

static inline Rfmalloc_cleanup_runtime_fun Rfmalloc_cleanup_runtime_ptr(void)
{
    return (Rfmalloc_cleanup_runtime_fun) R_GetCCallable("Rfmalloc", "Rfmalloc_cleanup_runtime");
}

static inline Rfmalloc_list_allocations_fun Rfmalloc_list_allocations_ptr(void)
{
    return (Rfmalloc_list_allocations_fun) R_GetCCallable("Rfmalloc", "Rfmalloc_list_allocations");
}

static inline Rfmalloc_set_default_runtime_fun Rfmalloc_set_default_runtime_ptr(void)
{
    return (Rfmalloc_set_default_runtime_fun) R_GetCCallable("Rfmalloc", "Rfmalloc_set_default_runtime");
}

static inline Rfmalloc_is_runtime_fun Rfmalloc_is_runtime_ptr(void)
{
    return (Rfmalloc_is_runtime_fun) R_GetCCallable("Rfmalloc", "Rfmalloc_is_runtime");
}

static inline Rfmalloc_is_fmalloc_vector_fun Rfmalloc_is_fmalloc_vector_ptr(void)
{
    return (Rfmalloc_is_fmalloc_vector_fun) R_GetCCallable("Rfmalloc", "Rfmalloc_is_fmalloc_vector");
}

static inline Rfmalloc_runtime_of_vector_fun Rfmalloc_runtime_of_vector_ptr(void)
{
    return (Rfmalloc_runtime_of_vector_fun) R_GetCCallable("Rfmalloc", "Rfmalloc_runtime_of_vector");
}

static inline Rfmalloc_runtime_info_fun Rfmalloc_runtime_info_ptr(void)
{
    return (Rfmalloc_runtime_info_fun) R_GetCCallable("Rfmalloc", "Rfmalloc_runtime_info");
}

static inline Rfmalloc_vector_type_fun Rfmalloc_vector_type_ptr(void)
{
    return (Rfmalloc_vector_type_fun) R_GetCCallable("Rfmalloc", "Rfmalloc_vector_type");
}

static inline Rfmalloc_vector_length_fun Rfmalloc_vector_length_ptr(void)
{
    return (Rfmalloc_vector_length_fun) R_GetCCallable("Rfmalloc", "Rfmalloc_vector_length");
}

static inline Rfmalloc_vector_payload_ptr_fun Rfmalloc_vector_payload_ptr_ptr(void)
{
    return (Rfmalloc_vector_payload_ptr_fun) R_GetCCallable("Rfmalloc", "Rfmalloc_vector_payload_ptr");
}

static inline Rfmalloc_vector_info_fun Rfmalloc_vector_info_ptr(void)
{
    return (Rfmalloc_vector_info_fun) R_GetCCallable("Rfmalloc", "Rfmalloc_vector_info");
}

static inline Rfmalloc_destroy_vector_fun Rfmalloc_destroy_vector_ptr(void)
{
    return (Rfmalloc_destroy_vector_fun) R_GetCCallable("Rfmalloc", "Rfmalloc_destroy_vector");
}

static inline Rfmalloc_register_tensor_codec_fun Rfmalloc_register_tensor_codec_ptr(void)
{
    return (Rfmalloc_register_tensor_codec_fun) R_GetCCallable("Rfmalloc", "Rfmalloc_register_tensor_codec");
}

static inline int Rfmalloc_api_version(void)
{
    return Rfmalloc_api_version_ptr()();
}

static inline SEXP Rfmalloc_default_runtime(void)
{
    return Rfmalloc_default_runtime_ptr()();
}

static inline SEXP Rfmalloc_open_runtime(const char *filepath, double size_gb,
                                         const char *mode)
{
    return Rfmalloc_open_runtime_ptr()(filepath, size_gb, mode);
}

static inline SEXP Rfmalloc_create_vector(SEXP runtime, int sexptype,
                                          R_xlen_t length)
{
    return Rfmalloc_create_vector_ptr()(runtime, sexptype, length);
}

static inline SEXP Rfmalloc_create_vector_ex(SEXP runtime, int sexptype,
                                             R_xlen_t length,
                                             int zero_initialize)
{
    return Rfmalloc_create_vector_ex_ptr()(runtime, sexptype, length, zero_initialize);
}

static inline void Rfmalloc_cleanup_runtime(SEXP runtime)
{
    Rfmalloc_cleanup_runtime_ptr()(runtime);
}

static inline SEXP Rfmalloc_set_default_runtime(SEXP runtime)
{
    return Rfmalloc_set_default_runtime_ptr()(runtime);
}

static inline SEXP Rfmalloc_list_allocations(SEXP runtime)
{
    return Rfmalloc_list_allocations_ptr()(runtime);
}

static inline SEXP Rfmalloc_is_runtime(SEXP runtime)
{
    return Rfmalloc_is_runtime_ptr()(runtime);
}

static inline SEXP Rfmalloc_is_fmalloc_vector(SEXP vector)
{
    return Rfmalloc_is_fmalloc_vector_ptr()(vector);
}

static inline SEXP Rfmalloc_runtime_of_vector(SEXP vector)
{
    return Rfmalloc_runtime_of_vector_ptr()(vector);
}

static inline SEXP Rfmalloc_runtime_info(SEXP runtime)
{
    return Rfmalloc_runtime_info_ptr()(runtime);
}

static inline SEXP Rfmalloc_vector_type(SEXP vector)
{
    return Rfmalloc_vector_type_ptr()(vector);
}

static inline SEXP Rfmalloc_vector_length(SEXP vector)
{
    return Rfmalloc_vector_length_ptr()(vector);
}

static inline SEXP Rfmalloc_vector_payload_ptr(SEXP vector)
{
    return Rfmalloc_vector_payload_ptr_ptr()(vector);
}

static inline SEXP Rfmalloc_vector_info(SEXP vector)
{
    return Rfmalloc_vector_info_ptr()(vector);
}

static inline SEXP Rfmalloc_destroy_vector(SEXP vector, int unsafe)
{
    return Rfmalloc_destroy_vector_ptr()(vector, unsafe);
}

static inline int Rfmalloc_register_tensor_codec(const char *name,
                                                 unsigned int items_per_block,
                                                 unsigned int bytes_per_block,
                                                 Rfmalloc_tensor_decode_fn decode)
{
    return Rfmalloc_register_tensor_codec_ptr()(name, items_per_block,
                                                bytes_per_block, decode);
}

static inline Rfmalloc_register_matmul_backend_fun Rfmalloc_register_matmul_backend_ptr(void)
{
    return (Rfmalloc_register_matmul_backend_fun) R_GetCCallable("Rfmalloc", "Rfmalloc_register_matmul_backend");
}

static inline int Rfmalloc_register_matmul_backend(const char *name, Rfmalloc_gemm_fn fn)
{
    return Rfmalloc_register_matmul_backend_ptr()(name, fn);
}

static inline Rfmalloc_register_matmul_backend_ex_fun Rfmalloc_register_matmul_backend_ex_ptr(void)
{
    return (Rfmalloc_register_matmul_backend_ex_fun) R_GetCCallable("Rfmalloc", "Rfmalloc_register_matmul_backend_ex");
}

static inline int Rfmalloc_register_matmul_backend_ex(const char *name,
                                                      Rfmalloc_gemm_fn fn,
                                                      Rfmalloc_typed_gemm_fn typed_fn)
{
    return Rfmalloc_register_matmul_backend_ex_ptr()(name, fn, typed_fn);
}

#ifdef __cplusplus
}
#endif

#endif /* RFMALLOC_API_H */
