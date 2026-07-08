#ifndef RGGML_API_PUBLIC_H
#define RGGML_API_PUBLIC_H

/*
 * Rggml.h - C-callable compute API for the Rggml carrier package, version 1.
 *
 * Rggml vendors the CPU backend of the GGML tensor library
 * (https://github.com/ggml-org/ggml) as a static library and exposes a
 * minimal, clean C API to it via R_RegisterCCallable(). Other R packages
 * link to Rggml ('LinkingTo: Rggml' in DESCRIPTION) and #include this
 * header to build and compute GGML tensor graphs - including quantized
 * types such as GGML_TYPE_Q4_K - from their own C or C++ code, without
 * re-vendoring GGML themselves.
 *
 * Usage:
 *   1. List Rggml under LinkingTo (and Imports, so it is loaded first) in
 *      your package's DESCRIPTION.
 *   2. #include <Rggml.h> in your C/C++ source. This header pulls in the
 *      vendored public GGML headers (ggml.h, ggml-alloc.h, ggml-backend.h,
 *      ggml-cpu.h), also installed by Rggml, so real GGML types
 *      (struct ggml_context *, struct ggml_tensor *, ggml_backend_t, ...)
 *      are available in your signatures.
 *   3. Every wrapped function below has a "<Name>_ptr()" accessor that
 *      resolves the symbol with R_GetCCallable() and returns a function
 *      pointer. Call it once (e.g. cache the pointer in a static, or in
 *      your package's .onLoad-triggered init routine) and invoke through
 *      it:
 *
 *          Rggml_context_create_fun ctx_create = Rggml_context_create_ptr();
 *          struct ggml_context *ctx = ctx_create(1 << 20, 0);
 *
 *      R_GetCCallable() aborts the R session if Rggml's namespace has not
 *      been loaded yet, so only call these accessors after Rggml is
 *      guaranteed to be attached/loaded (normal LinkingTo + Imports
 *      behavior guarantees this by the time your package's own code runs).
 *
 * Returned/accepted GGML pointers (ggml_context *, ggml_tensor *, ...)
 * follow plain GGML ownership rules, not R's SEXP protection rules: there
 * is no PROTECT/UNPROTECT here, only the usual ggml_init()/ggml_free() and
 * ggml_backend_*_init()/ggml_backend_free() lifecycle pairs.
 */

#include <ggml.h>
#include <ggml-alloc.h>
#include <ggml-backend.h>
#include <ggml-cpu.h>

#include <R_ext/Rdynload.h>

#ifdef __cplusplus
extern "C" {
#endif

/* -- version / identity -------------------------------------------------- */

typedef int (*Rggml_api_version_fun)(void);
typedef const char *(*Rggml_version_fun)(void);

/* -- context lifecycle ---------------------------------------------------- */

typedef struct ggml_context *(*Rggml_context_create_fun)(size_t mem_size, int no_alloc);
typedef void (*Rggml_context_free_fun)(struct ggml_context *ctx);
typedef size_t (*Rggml_used_mem_fun)(const struct ggml_context *ctx);
typedef size_t (*Rggml_tensor_overhead_fun)(void);
typedef size_t (*Rggml_graph_overhead_fun)(size_t size);

/* -- tensor creation / introspection -------------------------------------- */

typedef struct ggml_tensor *(*Rggml_new_tensor_fun)(struct ggml_context *ctx, enum ggml_type type,
                                                     int n_dims, const int64_t *ne, void *data);
typedef void *(*Rggml_tensor_data_fun)(const struct ggml_tensor *tensor);
typedef void (*Rggml_tensor_set_data_fun)(struct ggml_tensor *tensor, void *data);
typedef int64_t (*Rggml_tensor_ne_fun)(const struct ggml_tensor *tensor, int dim);
typedef size_t (*Rggml_tensor_nb_fun)(const struct ggml_tensor *tensor, int dim);

/* -- CPU backend ----------------------------------------------------------- */

typedef ggml_backend_t (*Rggml_backend_cpu_init_fun)(void);
typedef void (*Rggml_backend_free_fun)(ggml_backend_t backend);
typedef int (*Rggml_backend_graph_compute_fun)(ggml_backend_t backend, struct ggml_cgraph *cgraph);

/* -- BLAS backend ---------------------------------------------------------- */
/*
 * GGML's BLAS backend offloads dense F32 (and F16/quantized, after an internal
 * to-float conversion) mul_mat to whatever BLAS the R build links against
 * (reference libRblas, OpenBLAS, MKL, Accelerate, ...). It computes on host
 * memory, so a backend from Rggml_backend_blas_init() is a drop-in `backend`
 * for Rggml_compute_mul_mat()/Rggml_backend_graph_compute() on mul_mat graphs.
 * Returns NULL if the BLAS backend is unavailable. Free with Rggml_backend_free().
 */
typedef ggml_backend_t (*Rggml_backend_blas_init_fun)(void);
typedef void (*Rggml_backend_blas_set_n_threads_fun)(ggml_backend_t backend_blas, int n_threads);

/* -- graph building ---------------------------------------------------------- */

typedef struct ggml_cgraph *(*Rggml_new_graph_fun)(struct ggml_context *ctx, size_t size);
typedef void (*Rggml_build_forward_expand_fun)(struct ggml_cgraph *cgraph, struct ggml_tensor *tensor);

/* -- matrix multiply --------------------------------------------------------- */

typedef struct ggml_tensor *(*Rggml_mul_mat_fun)(struct ggml_context *ctx, struct ggml_tensor *a,
                                                  struct ggml_tensor *b);
typedef int (*Rggml_compute_mul_mat_fun)(struct ggml_context *ctx, ggml_backend_t backend,
                                         struct ggml_tensor *a, struct ggml_tensor *b,
                                         float *out_f32, double *out_f64);

/* -- quantization ------------------------------------------------------------- */

/* Quantize `nrows` rows of `n_per_row` f32 values into `type`'s block format
 * (e.g. GGML_TYPE_Q4_K); `dst` must hold Rggml_row_size(type, n_per_row) *
 * nrows bytes. Returns bytes written. The output is byte-compatible with a
 * GGUF tensor of that type. */
typedef size_t (*Rggml_quantize_fun)(enum ggml_type type, const float *src, void *dst,
                                     int64_t nrows, int64_t n_per_row);

/* Decode `n` elements (a whole number of blocks) of a quantized payload to f32
 * through GGML's type-traits to_float - the authoritative reference
 * dequantizer for every GGUF type. The caller must have initialized the CPU
 * backend once (Rggml_backend_cpu_init) so the fp16 table used to read block
 * scales is populated. Returns 0 on success. */
typedef int (*Rggml_dequantize_fun)(enum ggml_type type, const void *src,
                                    float *dst, int64_t n);

/* -- graph ops (API version 5) ------------------------------------------------ */
/*
 * The ggml ops a transformer forward pass composes. Scalar parameters cross
 * the boundary as double. Rggml_rope wraps ggml_rope_ext with YaRN off
 * (ext_factor 0, attn_factor 1, freq_scale 1, n_ctx_orig 0); `pos` is an I32
 * positions tensor; mode is GGML_ROPE_TYPE_NORMAL (0) or _NEOX (2).
 */
typedef struct ggml_tensor *(*Rggml_get_rows_fun)(struct ggml_context *ctx,
                                                   struct ggml_tensor *a,
                                                   struct ggml_tensor *b);
typedef struct ggml_tensor *(*Rggml_rms_norm_fun)(struct ggml_context *ctx,
                                                   struct ggml_tensor *a, double eps);
typedef struct ggml_tensor *(*Rggml_mul_fun)(struct ggml_context *ctx,
                                              struct ggml_tensor *a,
                                              struct ggml_tensor *b);
typedef struct ggml_tensor *(*Rggml_add_fun)(struct ggml_context *ctx,
                                              struct ggml_tensor *a,
                                              struct ggml_tensor *b);
typedef struct ggml_tensor *(*Rggml_silu_fun)(struct ggml_context *ctx,
                                               struct ggml_tensor *a);
typedef struct ggml_tensor *(*Rggml_scale_fun)(struct ggml_context *ctx,
                                                struct ggml_tensor *a, double s);
typedef struct ggml_tensor *(*Rggml_soft_max_fun)(struct ggml_context *ctx,
                                                   struct ggml_tensor *a);
typedef struct ggml_tensor *(*Rggml_diag_mask_inf_fun)(struct ggml_context *ctx,
                                                        struct ggml_tensor *a,
                                                        int n_past);
typedef struct ggml_tensor *(*Rggml_rope_fun)(struct ggml_context *ctx,
                                               struct ggml_tensor *a,
                                               struct ggml_tensor *pos,
                                               int n_dims, int mode, double freq_base);
typedef struct ggml_tensor *(*Rggml_reshape_2d_fun)(struct ggml_context *ctx,
                                                     struct ggml_tensor *a,
                                                     int64_t ne0, int64_t ne1);
typedef struct ggml_tensor *(*Rggml_reshape_3d_fun)(struct ggml_context *ctx,
                                                     struct ggml_tensor *a,
                                                     int64_t ne0, int64_t ne1, int64_t ne2);
typedef struct ggml_tensor *(*Rggml_permute_fun)(struct ggml_context *ctx,
                                                  struct ggml_tensor *a,
                                                  int axis0, int axis1, int axis2, int axis3);
typedef struct ggml_tensor *(*Rggml_cont_fun)(struct ggml_context *ctx,
                                               struct ggml_tensor *a);
typedef struct ggml_tensor *(*Rggml_transpose_fun)(struct ggml_context *ctx,
                                                    struct ggml_tensor *a);

/* -- Vulkan backend (API version 7) -------------------------------------------- */
/* Always resolvable; reports 0 devices and refuses to init unless Rggml was
 * built with --with-vulkan. Free with Rggml_backend_free(). */
typedef int (*Rggml_backend_vulkan_device_count_fun)(void);
typedef ggml_backend_t (*Rggml_backend_vulkan_init_fun)(int device);
typedef int (*Rggml_backend_vulkan_device_description_fun)(int device, char *buf,
                                                            size_t buf_size);

/* -- device-buffer residency (API version 7) ----------------------------------- */
/* The backend-agnostic allocate/upload/compute/download path. CPU and BLAS
 * compute on host memory, but a GPU backend's tensors must live in device
 * memory: allocate every tensor of a no_alloc context into one backend buffer,
 * upload inputs, compute, download the result. Free the buffer before the ctx. */
typedef ggml_backend_buffer_t (*Rggml_backend_alloc_ctx_tensors_fun)(struct ggml_context *ctx,
                                                                      ggml_backend_t backend);
typedef void (*Rggml_backend_buffer_free_fun)(ggml_backend_buffer_t buffer);
typedef void (*Rggml_backend_tensor_set_fun)(struct ggml_tensor *tensor, const void *data,
                                              size_t offset, size_t size);
typedef void (*Rggml_backend_tensor_get_fun)(const struct ggml_tensor *tensor, void *data,
                                              size_t offset, size_t size);

/* -- views and copies (API version 6) ----------------------------------------- */
/* Strided views into a tensor (offsets/strides in bytes, as in ggml) and the
 * copy op - the building blocks of a KV cache: cpy nodes write new K/V into
 * views of a persistent cache tensor, expanded into the graph ahead of the
 * attention nodes that read other views of the same cache. */
typedef struct ggml_tensor *(*Rggml_view_1d_fun)(struct ggml_context *ctx,
                                                  struct ggml_tensor *a,
                                                  int64_t ne0, size_t offset);
typedef struct ggml_tensor *(*Rggml_view_2d_fun)(struct ggml_context *ctx,
                                                  struct ggml_tensor *a,
                                                  int64_t ne0, int64_t ne1,
                                                  size_t nb1, size_t offset);
typedef struct ggml_tensor *(*Rggml_view_3d_fun)(struct ggml_context *ctx,
                                                  struct ggml_tensor *a,
                                                  int64_t ne0, int64_t ne1, int64_t ne2,
                                                  size_t nb1, size_t nb2, size_t offset);
typedef struct ggml_tensor *(*Rggml_cpy_fun)(struct ggml_context *ctx,
                                              struct ggml_tensor *a,
                                              struct ggml_tensor *b);

/* -- type/size introspection -------------------------------------------------- */

typedef size_t (*Rggml_type_size_fun)(enum ggml_type type);
typedef size_t (*Rggml_row_size_fun)(enum ggml_type type, int64_t ne);
typedef int64_t (*Rggml_blck_size_fun)(enum ggml_type type);
typedef size_t (*Rggml_nbytes_fun)(const struct ggml_tensor *tensor);
typedef int64_t (*Rggml_nelements_fun)(const struct ggml_tensor *tensor);
typedef const char *(*Rggml_type_name_fun)(enum ggml_type type);

/* -------------------------------------------------------------------------- */
/* R_GetCCallable() accessors                                                 */
/* -------------------------------------------------------------------------- */

static inline Rggml_api_version_fun Rggml_api_version_ptr(void)
{
    return (Rggml_api_version_fun) R_GetCCallable("Rggml", "Rggml_api_version");
}

static inline Rggml_version_fun Rggml_version_ptr(void)
{
    return (Rggml_version_fun) R_GetCCallable("Rggml", "Rggml_version");
}

static inline Rggml_context_create_fun Rggml_context_create_ptr(void)
{
    return (Rggml_context_create_fun) R_GetCCallable("Rggml", "Rggml_context_create");
}

static inline Rggml_context_free_fun Rggml_context_free_ptr(void)
{
    return (Rggml_context_free_fun) R_GetCCallable("Rggml", "Rggml_context_free");
}

static inline Rggml_used_mem_fun Rggml_used_mem_ptr(void)
{
    return (Rggml_used_mem_fun) R_GetCCallable("Rggml", "Rggml_used_mem");
}

static inline Rggml_tensor_overhead_fun Rggml_tensor_overhead_ptr(void)
{
    return (Rggml_tensor_overhead_fun) R_GetCCallable("Rggml", "Rggml_tensor_overhead");
}

static inline Rggml_graph_overhead_fun Rggml_graph_overhead_ptr(void)
{
    return (Rggml_graph_overhead_fun) R_GetCCallable("Rggml", "Rggml_graph_overhead");
}

static inline Rggml_new_tensor_fun Rggml_new_tensor_ptr(void)
{
    return (Rggml_new_tensor_fun) R_GetCCallable("Rggml", "Rggml_new_tensor");
}

static inline Rggml_tensor_data_fun Rggml_tensor_data_ptr(void)
{
    return (Rggml_tensor_data_fun) R_GetCCallable("Rggml", "Rggml_tensor_data");
}

static inline Rggml_tensor_set_data_fun Rggml_tensor_set_data_ptr(void)
{
    return (Rggml_tensor_set_data_fun) R_GetCCallable("Rggml", "Rggml_tensor_set_data");
}

static inline Rggml_tensor_ne_fun Rggml_tensor_ne_ptr(void)
{
    return (Rggml_tensor_ne_fun) R_GetCCallable("Rggml", "Rggml_tensor_ne");
}

static inline Rggml_tensor_nb_fun Rggml_tensor_nb_ptr(void)
{
    return (Rggml_tensor_nb_fun) R_GetCCallable("Rggml", "Rggml_tensor_nb");
}

static inline Rggml_backend_cpu_init_fun Rggml_backend_cpu_init_ptr(void)
{
    return (Rggml_backend_cpu_init_fun) R_GetCCallable("Rggml", "Rggml_backend_cpu_init");
}

static inline Rggml_backend_free_fun Rggml_backend_free_ptr(void)
{
    return (Rggml_backend_free_fun) R_GetCCallable("Rggml", "Rggml_backend_free");
}

static inline Rggml_backend_graph_compute_fun Rggml_backend_graph_compute_ptr(void)
{
    return (Rggml_backend_graph_compute_fun) R_GetCCallable("Rggml", "Rggml_backend_graph_compute");
}

static inline Rggml_backend_blas_init_fun Rggml_backend_blas_init_ptr(void)
{
    return (Rggml_backend_blas_init_fun) R_GetCCallable("Rggml", "Rggml_backend_blas_init");
}

static inline Rggml_backend_blas_set_n_threads_fun Rggml_backend_blas_set_n_threads_ptr(void)
{
    return (Rggml_backend_blas_set_n_threads_fun) R_GetCCallable("Rggml", "Rggml_backend_blas_set_n_threads");
}

static inline Rggml_new_graph_fun Rggml_new_graph_ptr(void)
{
    return (Rggml_new_graph_fun) R_GetCCallable("Rggml", "Rggml_new_graph");
}

static inline Rggml_build_forward_expand_fun Rggml_build_forward_expand_ptr(void)
{
    return (Rggml_build_forward_expand_fun) R_GetCCallable("Rggml", "Rggml_build_forward_expand");
}

static inline Rggml_mul_mat_fun Rggml_mul_mat_ptr(void)
{
    return (Rggml_mul_mat_fun) R_GetCCallable("Rggml", "Rggml_mul_mat");
}

static inline Rggml_compute_mul_mat_fun Rggml_compute_mul_mat_ptr(void)
{
    return (Rggml_compute_mul_mat_fun) R_GetCCallable("Rggml", "Rggml_compute_mul_mat");
}

static inline Rggml_quantize_fun Rggml_quantize_ptr(void)
{
    return (Rggml_quantize_fun) R_GetCCallable("Rggml", "Rggml_quantize");
}

static inline Rggml_dequantize_fun Rggml_dequantize_ptr(void)
{
    return (Rggml_dequantize_fun) R_GetCCallable("Rggml", "Rggml_dequantize");
}

static inline Rggml_get_rows_fun Rggml_get_rows_ptr(void)
{
    return (Rggml_get_rows_fun) R_GetCCallable("Rggml", "Rggml_get_rows");
}

static inline Rggml_rms_norm_fun Rggml_rms_norm_ptr(void)
{
    return (Rggml_rms_norm_fun) R_GetCCallable("Rggml", "Rggml_rms_norm");
}

static inline Rggml_mul_fun Rggml_mul_ptr(void)
{
    return (Rggml_mul_fun) R_GetCCallable("Rggml", "Rggml_mul");
}

static inline Rggml_add_fun Rggml_add_ptr(void)
{
    return (Rggml_add_fun) R_GetCCallable("Rggml", "Rggml_add");
}

static inline Rggml_silu_fun Rggml_silu_ptr(void)
{
    return (Rggml_silu_fun) R_GetCCallable("Rggml", "Rggml_silu");
}

static inline Rggml_scale_fun Rggml_scale_ptr(void)
{
    return (Rggml_scale_fun) R_GetCCallable("Rggml", "Rggml_scale");
}

static inline Rggml_soft_max_fun Rggml_soft_max_ptr(void)
{
    return (Rggml_soft_max_fun) R_GetCCallable("Rggml", "Rggml_soft_max");
}

static inline Rggml_diag_mask_inf_fun Rggml_diag_mask_inf_ptr(void)
{
    return (Rggml_diag_mask_inf_fun) R_GetCCallable("Rggml", "Rggml_diag_mask_inf");
}

static inline Rggml_rope_fun Rggml_rope_ptr(void)
{
    return (Rggml_rope_fun) R_GetCCallable("Rggml", "Rggml_rope");
}

static inline Rggml_reshape_2d_fun Rggml_reshape_2d_ptr(void)
{
    return (Rggml_reshape_2d_fun) R_GetCCallable("Rggml", "Rggml_reshape_2d");
}

static inline Rggml_reshape_3d_fun Rggml_reshape_3d_ptr(void)
{
    return (Rggml_reshape_3d_fun) R_GetCCallable("Rggml", "Rggml_reshape_3d");
}

static inline Rggml_permute_fun Rggml_permute_ptr(void)
{
    return (Rggml_permute_fun) R_GetCCallable("Rggml", "Rggml_permute");
}

static inline Rggml_cont_fun Rggml_cont_ptr(void)
{
    return (Rggml_cont_fun) R_GetCCallable("Rggml", "Rggml_cont");
}

static inline Rggml_transpose_fun Rggml_transpose_ptr(void)
{
    return (Rggml_transpose_fun) R_GetCCallable("Rggml", "Rggml_transpose");
}

static inline Rggml_backend_vulkan_device_count_fun Rggml_backend_vulkan_device_count_ptr(void)
{
    return (Rggml_backend_vulkan_device_count_fun) R_GetCCallable("Rggml", "Rggml_backend_vulkan_device_count");
}

static inline Rggml_backend_vulkan_init_fun Rggml_backend_vulkan_init_ptr(void)
{
    return (Rggml_backend_vulkan_init_fun) R_GetCCallable("Rggml", "Rggml_backend_vulkan_init");
}

static inline Rggml_backend_vulkan_device_description_fun Rggml_backend_vulkan_device_description_ptr(void)
{
    return (Rggml_backend_vulkan_device_description_fun) R_GetCCallable("Rggml", "Rggml_backend_vulkan_device_description");
}

static inline Rggml_backend_alloc_ctx_tensors_fun Rggml_backend_alloc_ctx_tensors_ptr(void)
{
    return (Rggml_backend_alloc_ctx_tensors_fun) R_GetCCallable("Rggml", "Rggml_backend_alloc_ctx_tensors");
}

static inline Rggml_backend_buffer_free_fun Rggml_backend_buffer_free_ptr(void)
{
    return (Rggml_backend_buffer_free_fun) R_GetCCallable("Rggml", "Rggml_backend_buffer_free");
}

static inline Rggml_backend_tensor_set_fun Rggml_backend_tensor_set_ptr(void)
{
    return (Rggml_backend_tensor_set_fun) R_GetCCallable("Rggml", "Rggml_backend_tensor_set");
}

static inline Rggml_backend_tensor_get_fun Rggml_backend_tensor_get_ptr(void)
{
    return (Rggml_backend_tensor_get_fun) R_GetCCallable("Rggml", "Rggml_backend_tensor_get");
}

static inline Rggml_view_1d_fun Rggml_view_1d_ptr(void)
{
    return (Rggml_view_1d_fun) R_GetCCallable("Rggml", "Rggml_view_1d");
}

static inline Rggml_view_2d_fun Rggml_view_2d_ptr(void)
{
    return (Rggml_view_2d_fun) R_GetCCallable("Rggml", "Rggml_view_2d");
}

static inline Rggml_view_3d_fun Rggml_view_3d_ptr(void)
{
    return (Rggml_view_3d_fun) R_GetCCallable("Rggml", "Rggml_view_3d");
}

static inline Rggml_cpy_fun Rggml_cpy_ptr(void)
{
    return (Rggml_cpy_fun) R_GetCCallable("Rggml", "Rggml_cpy");
}

static inline Rggml_type_size_fun Rggml_type_size_ptr(void)
{
    return (Rggml_type_size_fun) R_GetCCallable("Rggml", "Rggml_type_size");
}

static inline Rggml_row_size_fun Rggml_row_size_ptr(void)
{
    return (Rggml_row_size_fun) R_GetCCallable("Rggml", "Rggml_row_size");
}

static inline Rggml_blck_size_fun Rggml_blck_size_ptr(void)
{
    return (Rggml_blck_size_fun) R_GetCCallable("Rggml", "Rggml_blck_size");
}

static inline Rggml_nbytes_fun Rggml_nbytes_ptr(void)
{
    return (Rggml_nbytes_fun) R_GetCCallable("Rggml", "Rggml_nbytes");
}

static inline Rggml_nelements_fun Rggml_nelements_ptr(void)
{
    return (Rggml_nelements_fun) R_GetCCallable("Rggml", "Rggml_nelements");
}

static inline Rggml_type_name_fun Rggml_type_name_ptr(void)
{
    return (Rggml_type_name_fun) R_GetCCallable("Rggml", "Rggml_type_name");
}

#ifdef __cplusplus
}
#endif

#endif /* RGGML_API_PUBLIC_H */
