/*
 * rggml_api.c - Rggml's C-callable compute API.
 *
 * Thin, clean `extern "C"` wrapper functions around the vendored GGML CPU
 * backend. These are the functions registered with R_RegisterCCallable() in
 * rggml_init.c and re-exposed to downstream packages via the inline
 * R_GetCCallable() wrappers in inst/include/Rggml.h.
 *
 * Design intent: downstream packages build and compute GGML tensor graphs
 * using real GGML types (struct ggml_context *, struct ggml_tensor *,
 * ggml_backend_t, ...) - Rggml.h includes the vendored public GGML headers
 * so those types are available - and never need to vendor or link against
 * GGML themselves.
 */

#include <stdlib.h>
#include <string.h>

#include <ggml.h>
#include <ggml-alloc.h>
#include <ggml-backend.h>
#include <ggml-cpu.h>
#include <ggml-blas.h>

#include "rggml_api.h"

int Rggml_api_version(void)
{
    return RGGML_API_VERSION;
}

const char *Rggml_version(void)
{
    return ggml_version();
}

struct ggml_context *Rggml_context_create(size_t mem_size, int no_alloc)
{
    struct ggml_init_params params;
    memset(&params, 0, sizeof(params));
    params.mem_size = mem_size;
    params.mem_buffer = NULL;
    params.no_alloc = no_alloc != 0;
    return ggml_init(params);
}

void Rggml_context_free(struct ggml_context *ctx)
{
    if (ctx) ggml_free(ctx);
}

size_t Rggml_used_mem(const struct ggml_context *ctx)
{
    return ggml_used_mem(ctx);
}

size_t Rggml_tensor_overhead(void)
{
    return ggml_tensor_overhead();
}

size_t Rggml_graph_overhead(size_t size)
{
    return ggml_graph_overhead_custom(size, /*grads=*/false);
}

struct ggml_tensor *Rggml_new_tensor(struct ggml_context *ctx, enum ggml_type type,
                                      int n_dims, const int64_t *ne, void *data)
{
    if (!ctx || !ne || n_dims < 1 || n_dims > GGML_MAX_DIMS) return NULL;
    struct ggml_tensor *t = ggml_new_tensor(ctx, type, n_dims, ne);
    if (t && data) {
        /* Zero-copy path: point the tensor at caller-owned memory (e.g. an
         * mmap'd quantized payload). Intended for contexts created with
         * no_alloc = 1, where ggml_new_tensor() does not allocate a data
         * buffer of its own. */
        t->data = data;
    }
    return t;
}

void *Rggml_tensor_data(const struct ggml_tensor *tensor)
{
    return tensor ? tensor->data : NULL;
}

void Rggml_tensor_set_data(struct ggml_tensor *tensor, void *data)
{
    if (tensor) tensor->data = data;
}

int64_t Rggml_tensor_ne(const struct ggml_tensor *tensor, int dim)
{
    if (!tensor || dim < 0 || dim >= GGML_MAX_DIMS) return -1;
    return tensor->ne[dim];
}

size_t Rggml_tensor_nb(const struct ggml_tensor *tensor, int dim)
{
    if (!tensor || dim < 0 || dim >= GGML_MAX_DIMS) return 0;
    return tensor->nb[dim];
}

ggml_backend_t Rggml_backend_cpu_init(void)
{
    return ggml_backend_cpu_init();
}

void Rggml_backend_free(ggml_backend_t backend)
{
    if (backend) ggml_backend_free(backend);
}

int Rggml_backend_graph_compute(ggml_backend_t backend, struct ggml_cgraph *cgraph)
{
    if (!backend || !cgraph) return (int)GGML_STATUS_FAILED;
    return (int)ggml_backend_graph_compute(backend, cgraph);
}

ggml_backend_t Rggml_backend_blas_init(void)
{
    return ggml_backend_blas_init();
}

void Rggml_backend_blas_set_n_threads(ggml_backend_t backend_blas, int n_threads)
{
    if (backend_blas) ggml_backend_blas_set_n_threads(backend_blas, n_threads);
}

struct ggml_cgraph *Rggml_new_graph(struct ggml_context *ctx, size_t size)
{
    if (!ctx) return NULL;
    return ggml_new_graph_custom(ctx, size > 0 ? size : GGML_DEFAULT_GRAPH_SIZE, /*grads=*/false);
}

void Rggml_build_forward_expand(struct ggml_cgraph *cgraph, struct ggml_tensor *tensor)
{
    if (cgraph && tensor) ggml_build_forward_expand(cgraph, tensor);
}

struct ggml_tensor *Rggml_mul_mat(struct ggml_context *ctx, struct ggml_tensor *a, struct ggml_tensor *b)
{
    if (!ctx || !a || !b) return NULL;
    return ggml_mul_mat(ctx, a, b);
}

/*
 * Rggml_compute_mul_mat(): build the one-op graph `ggml_mul_mat(ctx, a, b)`,
 * compute it on `backend`, and copy the F32 result into a caller-provided
 * buffer (out_f32 XOR out_f64; pass NULL for the one you don't want).
 *
 * ggml_mul_mat()'s result is always GGML_TYPE_F32 regardless of the input
 * types (this holds for quantized inputs too - dequantization happens
 * internally during the dot-product accumulation). This helper allocates
 * scratch memory for the result tensor itself (it does not require `ctx`
 * to have room for the result's *data*, only for its small tensor/graph
 * metadata) so it works whether `ctx` was created with no_alloc = 0 or 1.
 *
 * Returns 0 on success, a negative value on failure.
 */
int Rggml_compute_mul_mat(struct ggml_context *ctx, ggml_backend_t backend,
                           struct ggml_tensor *a, struct ggml_tensor *b,
                           float *out_f32, double *out_f64)
{
    if (!ctx || !backend || !a || !b) return -1;
    if (!out_f32 && !out_f64) return -1;

    struct ggml_tensor *result = ggml_mul_mat(ctx, a, b);
    if (!result) return -2;

    size_t nbytes = ggml_nbytes(result);
    void *result_data = malloc(nbytes > 0 ? nbytes : 1);
    if (!result_data) return -3;
    result->data = result_data;

    struct ggml_cgraph *gf = ggml_new_graph_custom(ctx, 8, /*grads=*/false);
    if (!gf) {
        free(result_data);
        return -4;
    }
    ggml_build_forward_expand(gf, result);

    enum ggml_status status = ggml_backend_graph_compute(backend, gf);
    if (status != GGML_STATUS_SUCCESS) {
        free(result_data);
        return -5;
    }

    int64_t n = ggml_nelements(result);
    if (out_f32) {
        memcpy(out_f32, result_data, (size_t)n * sizeof(float));
    }
    if (out_f64) {
        const float *src = (const float *)result_data;
        for (int64_t i = 0; i < n; i++) out_f64[i] = (double)src[i];
    }

    free(result_data);
    return 0;
}

size_t Rggml_type_size(enum ggml_type type)
{
    return ggml_type_size(type);
}

size_t Rggml_row_size(enum ggml_type type, int64_t ne)
{
    return ggml_row_size(type, ne);
}

int64_t Rggml_blck_size(enum ggml_type type)
{
    return ggml_blck_size(type);
}

size_t Rggml_nbytes(const struct ggml_tensor *tensor)
{
    return tensor ? ggml_nbytes(tensor) : 0;
}

int64_t Rggml_nelements(const struct ggml_tensor *tensor)
{
    return tensor ? ggml_nelements(tensor) : 0;
}

const char *Rggml_type_name(enum ggml_type type)
{
    return ggml_type_name(type);
}
