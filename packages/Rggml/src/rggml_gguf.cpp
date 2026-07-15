/*
 * Official GGUF service for sibling packages. The parser and writer are GGML's
 * own gguf.cpp; this file only turns its many concrete entry points into two
 * small opaque contexts suitable for R_RegisterCCallable().
 */
#include <cstring>
#include <memory>
#include <new>
#include <vector>

#include <ggml.h>
#include <gguf.h>

#include "rggml_api.h"

struct Rggml_gguf_context {
    struct gguf_context *meta;
    struct ggml_context *tensors;
};

struct Rggml_gguf_writer {
    struct gguf_context *meta;
    std::vector<std::vector<float>> data;
};

extern "C" {

Rggml_gguf_context *Rggml_gguf_open(const char *path)
{
    if (!path) return nullptr;
    std::unique_ptr<Rggml_gguf_context> out(
        new (std::nothrow) Rggml_gguf_context{nullptr, nullptr});
    if (!out) return nullptr;

    struct gguf_init_params params = {/*no_alloc=*/true,
                                      /*ctx=*/&out->tensors};
    out->meta = gguf_init_from_file(path, params);
    if (!out->meta) {
        if (out->tensors) ggml_free(out->tensors);
        return nullptr;
    }
    return out.release();
}

void Rggml_gguf_close(Rggml_gguf_context *ctx)
{
    if (!ctx) return;
    if (ctx->tensors) ggml_free(ctx->tensors);
    if (ctx->meta) gguf_free(ctx->meta);
    delete ctx;
}

uint32_t Rggml_gguf_version(const Rggml_gguf_context *ctx)
{
    return ctx && ctx->meta ? gguf_get_version(ctx->meta) : 0;
}

size_t Rggml_gguf_data_offset(const Rggml_gguf_context *ctx)
{
    return ctx && ctx->meta ? gguf_get_data_offset(ctx->meta) : 0;
}

int64_t Rggml_gguf_n_kv(const Rggml_gguf_context *ctx)
{
    return ctx && ctx->meta ? gguf_get_n_kv(ctx->meta) : -1;
}

int Rggml_gguf_kv(const Rggml_gguf_context *ctx, int64_t id,
                  struct Rggml_gguf_kv *out)
{
    if (!ctx || !ctx->meta || !out || id < 0 || id >= gguf_get_n_kv(ctx->meta))
        return -1;

    const enum gguf_type stored = gguf_get_kv_type(ctx->meta, id);
    out->key = gguf_get_key(ctx->meta, id);
    out->is_array = stored == GGUF_TYPE_ARRAY;
    out->type = out->is_array ? (int)gguf_get_arr_type(ctx->meta, id)
                              : (int)stored;
    out->n = out->is_array ? gguf_get_arr_n(ctx->meta, id) : 1;
    out->data = nullptr;
    if (out->type != GGUF_TYPE_STRING) {
        out->data = out->is_array ? gguf_get_arr_data(ctx->meta, id)
                                  : gguf_get_val_data(ctx->meta, id);
    }
    return 0;
}

const char *Rggml_gguf_kv_string(const Rggml_gguf_context *ctx, int64_t id,
                                 size_t index)
{
    if (!ctx || !ctx->meta || id < 0 || id >= gguf_get_n_kv(ctx->meta))
        return nullptr;
    if (gguf_get_kv_type(ctx->meta, id) == GGUF_TYPE_ARRAY) {
        if (gguf_get_arr_type(ctx->meta, id) != GGUF_TYPE_STRING ||
            index >= gguf_get_arr_n(ctx->meta, id)) return nullptr;
        return gguf_get_arr_str(ctx->meta, id, index);
    }
    if (index != 0 || gguf_get_kv_type(ctx->meta, id) != GGUF_TYPE_STRING)
        return nullptr;
    return gguf_get_val_str(ctx->meta, id);
}

int64_t Rggml_gguf_n_tensors(const Rggml_gguf_context *ctx)
{
    return ctx && ctx->meta ? gguf_get_n_tensors(ctx->meta) : -1;
}

int64_t Rggml_gguf_find_tensor(const Rggml_gguf_context *ctx,
                               const char *name)
{
    return ctx && ctx->meta && name ? gguf_find_tensor(ctx->meta, name) : -1;
}

int Rggml_gguf_tensor(const Rggml_gguf_context *ctx, int64_t id,
                      struct Rggml_gguf_tensor *out)
{
    if (!ctx || !ctx->meta || !ctx->tensors || !out || id < 0 ||
        id >= gguf_get_n_tensors(ctx->meta)) return -1;

    const char *name = gguf_get_tensor_name(ctx->meta, id);
    const struct ggml_tensor *tensor = ggml_get_tensor(ctx->tensors, name);
    if (!tensor) return -1;

    out->name = name;
    out->type = gguf_get_tensor_type(ctx->meta, id);
    out->type_name = ggml_type_name(out->type);
    out->n_dims = ggml_n_dims(tensor);
    for (int i = 0; i < GGML_MAX_DIMS; ++i) out->ne[i] = tensor->ne[i];
    out->n_elements = ggml_nelements(tensor);
    out->nbytes = gguf_get_tensor_size(ctx->meta, id);
    out->offset = gguf_get_tensor_offset(ctx->meta, id);
    return 0;
}

Rggml_gguf_writer *Rggml_gguf_writer_open(void)
{
    std::unique_ptr<Rggml_gguf_writer> out(
        new (std::nothrow) Rggml_gguf_writer{gguf_init_empty(), {}});
    if (!out || !out->meta) return nullptr;
    return out.release();
}

void Rggml_gguf_writer_close(Rggml_gguf_writer *ctx)
{
    if (!ctx) return;
    if (ctx->meta) gguf_free(ctx->meta);
    delete ctx;
}

int Rggml_gguf_writer_set_string(Rggml_gguf_writer *ctx, const char *key,
                                  const char *value)
{
    if (!ctx || !ctx->meta || !key || !value) return -1;
    gguf_set_val_str(ctx->meta, key, value);
    return 0;
}

int Rggml_gguf_writer_set_strings(Rggml_gguf_writer *ctx, const char *key,
                                   const char **values, size_t n)
{
    if (!ctx || !ctx->meta || !key || (n && !values)) return -1;
    for (size_t i = 0; i < n; ++i) {
        if (!values[i]) return -1;
    }
    gguf_set_arr_str(ctx->meta, key, values, n);
    return 0;
}

int Rggml_gguf_writer_set_f64(Rggml_gguf_writer *ctx, const char *key,
                               double value)
{
    if (!ctx || !ctx->meta || !key) return -1;
    gguf_set_val_f64(ctx->meta, key, value);
    return 0;
}

int Rggml_gguf_writer_set_f64s(Rggml_gguf_writer *ctx, const char *key,
                                const double *values, size_t n)
{
    if (!ctx || !ctx->meta || !key || (n && !values)) return -1;
    gguf_set_arr_data(ctx->meta, key, GGUF_TYPE_FLOAT64, values, n);
    return 0;
}

int Rggml_gguf_writer_add_f32(Rggml_gguf_writer *ctx, const char *name,
                               int n_dims, const int64_t *ne,
                               const double *data)
{
    if (!ctx || !ctx->meta || !name || !ne || !data || n_dims < 1 ||
        n_dims > GGML_MAX_DIMS || gguf_find_tensor(ctx->meta, name) >= 0)
        return -1;

    size_t n = 1;
    for (int i = 0; i < n_dims; ++i) {
        if (ne[i] < 1 || (size_t)ne[i] > SIZE_MAX / n) return -1;
        n *= (size_t)ne[i];
    }
    try {
        ctx->data.emplace_back(n);
        for (size_t i = 0; i < n; ++i) ctx->data.back()[i] = (float)data[i];
    } catch (...) {
        return -1;
    }

    struct ggml_init_params params = {
        /*mem_size=*/ggml_tensor_overhead(),
        /*mem_buffer=*/nullptr,
        /*no_alloc=*/true,
    };
    struct ggml_context *tensor_ctx = ggml_init(params);
    if (!tensor_ctx) {
        ctx->data.pop_back();
        return -1;
    }
    struct ggml_tensor *tensor =
        ggml_new_tensor(tensor_ctx, GGML_TYPE_F32, n_dims, ne);
    if (!tensor) {
        ggml_free(tensor_ctx);
        ctx->data.pop_back();
        return -1;
    }
    ggml_set_name(tensor, name);
    tensor->data = ctx->data.back().data();
    gguf_add_tensor(ctx->meta, tensor);
    ggml_free(tensor_ctx);
    return 0;
}

int Rggml_gguf_writer_write(Rggml_gguf_writer *ctx, const char *path)
{
    return ctx && ctx->meta && path && gguf_write_to_file(ctx->meta, path, false)
        ? 0 : -1;
}

} /* extern "C" */
