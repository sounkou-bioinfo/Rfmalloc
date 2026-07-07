#ifndef RGGML_API_H
#define RGGML_API_H

/*
 * rggml_api.h - internal declarations for Rggml's C-callable implementation
 * functions (src/rggml_api.c). Shared between rggml_init.c (registration)
 * and rggml_test.c (the .Call smoke-test routine).
 *
 * This is NOT the installed header - downstream packages use
 * inst/include/Rggml.h instead, which declares the same functions behind
 * R_GetCCallable() lookups.
 */

#include <ggml.h>
#include <ggml-backend.h>

#define RGGML_API_VERSION 6

#ifdef __cplusplus
extern "C" {
#endif

int Rggml_api_version(void);
const char *Rggml_version(void);

struct ggml_context *Rggml_context_create(size_t mem_size, int no_alloc);
void Rggml_context_free(struct ggml_context *ctx);
size_t Rggml_used_mem(const struct ggml_context *ctx);
size_t Rggml_tensor_overhead(void);
size_t Rggml_graph_overhead(size_t size);

struct ggml_tensor *Rggml_new_tensor(struct ggml_context *ctx, enum ggml_type type,
                                      int n_dims, const int64_t *ne, void *data);
void *Rggml_tensor_data(const struct ggml_tensor *tensor);
void Rggml_tensor_set_data(struct ggml_tensor *tensor, void *data);
int64_t Rggml_tensor_ne(const struct ggml_tensor *tensor, int dim);
size_t Rggml_tensor_nb(const struct ggml_tensor *tensor, int dim);

ggml_backend_t Rggml_backend_cpu_init(void);
void Rggml_backend_free(ggml_backend_t backend);
int Rggml_backend_graph_compute(ggml_backend_t backend, struct ggml_cgraph *cgraph);

ggml_backend_t Rggml_backend_blas_init(void);
void Rggml_backend_blas_set_n_threads(ggml_backend_t backend_blas, int n_threads);

struct ggml_cgraph *Rggml_new_graph(struct ggml_context *ctx, size_t size);
void Rggml_build_forward_expand(struct ggml_cgraph *cgraph, struct ggml_tensor *tensor);

struct ggml_tensor *Rggml_mul_mat(struct ggml_context *ctx, struct ggml_tensor *a, struct ggml_tensor *b);
int Rggml_compute_mul_mat(struct ggml_context *ctx, ggml_backend_t backend,
                           struct ggml_tensor *a, struct ggml_tensor *b,
                           float *out_f32, double *out_f64);

size_t Rggml_quantize(enum ggml_type type, const float *src, void *dst,
                      int64_t nrows, int64_t n_per_row);
int Rggml_dequantize(enum ggml_type type, const void *src, float *dst, int64_t n);

/* graph ops (API version 5) */
struct ggml_tensor *Rggml_get_rows(struct ggml_context *ctx, struct ggml_tensor *a,
                                    struct ggml_tensor *b);
struct ggml_tensor *Rggml_rms_norm(struct ggml_context *ctx, struct ggml_tensor *a,
                                    double eps);
struct ggml_tensor *Rggml_mul(struct ggml_context *ctx, struct ggml_tensor *a,
                               struct ggml_tensor *b);
struct ggml_tensor *Rggml_add(struct ggml_context *ctx, struct ggml_tensor *a,
                               struct ggml_tensor *b);
struct ggml_tensor *Rggml_silu(struct ggml_context *ctx, struct ggml_tensor *a);
struct ggml_tensor *Rggml_scale(struct ggml_context *ctx, struct ggml_tensor *a,
                                 double s);
struct ggml_tensor *Rggml_soft_max(struct ggml_context *ctx, struct ggml_tensor *a);
struct ggml_tensor *Rggml_diag_mask_inf(struct ggml_context *ctx, struct ggml_tensor *a,
                                         int n_past);
struct ggml_tensor *Rggml_rope(struct ggml_context *ctx, struct ggml_tensor *a,
                                struct ggml_tensor *pos, int n_dims, int mode,
                                double freq_base);
struct ggml_tensor *Rggml_reshape_2d(struct ggml_context *ctx, struct ggml_tensor *a,
                                      int64_t ne0, int64_t ne1);
struct ggml_tensor *Rggml_reshape_3d(struct ggml_context *ctx, struct ggml_tensor *a,
                                      int64_t ne0, int64_t ne1, int64_t ne2);
struct ggml_tensor *Rggml_permute(struct ggml_context *ctx, struct ggml_tensor *a,
                                   int axis0, int axis1, int axis2, int axis3);
struct ggml_tensor *Rggml_cont(struct ggml_context *ctx, struct ggml_tensor *a);
struct ggml_tensor *Rggml_transpose(struct ggml_context *ctx, struct ggml_tensor *a);

/* views and copies (API version 6) */
struct ggml_tensor *Rggml_view_1d(struct ggml_context *ctx, struct ggml_tensor *a,
                                   int64_t ne0, size_t offset);
struct ggml_tensor *Rggml_view_2d(struct ggml_context *ctx, struct ggml_tensor *a,
                                   int64_t ne0, int64_t ne1, size_t nb1, size_t offset);
struct ggml_tensor *Rggml_view_3d(struct ggml_context *ctx, struct ggml_tensor *a,
                                   int64_t ne0, int64_t ne1, int64_t ne2,
                                   size_t nb1, size_t nb2, size_t offset);
struct ggml_tensor *Rggml_cpy(struct ggml_context *ctx, struct ggml_tensor *a,
                               struct ggml_tensor *b);

size_t Rggml_type_size(enum ggml_type type);
size_t Rggml_row_size(enum ggml_type type, int64_t ne);
int64_t Rggml_blck_size(enum ggml_type type);
size_t Rggml_nbytes(const struct ggml_tensor *tensor);
int64_t Rggml_nelements(const struct ggml_tensor *tensor);
const char *Rggml_type_name(enum ggml_type type);

#ifdef __cplusplus
}
#endif

#endif /* RGGML_API_H */
