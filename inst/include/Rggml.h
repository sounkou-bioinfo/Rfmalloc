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

/* -- graph building ---------------------------------------------------------- */

typedef struct ggml_cgraph *(*Rggml_new_graph_fun)(struct ggml_context *ctx, size_t size);
typedef void (*Rggml_build_forward_expand_fun)(struct ggml_cgraph *cgraph, struct ggml_tensor *tensor);

/* -- matrix multiply --------------------------------------------------------- */

typedef struct ggml_tensor *(*Rggml_mul_mat_fun)(struct ggml_context *ctx, struct ggml_tensor *a,
                                                  struct ggml_tensor *b);
typedef int (*Rggml_compute_mul_mat_fun)(struct ggml_context *ctx, ggml_backend_t backend,
                                         struct ggml_tensor *a, struct ggml_tensor *b,
                                         float *out_f32, double *out_f64);

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
