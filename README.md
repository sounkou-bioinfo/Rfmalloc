# Rggml

Rggml is a **carrier package**: it vendors the CPU backend of the
[GGML](https://github.com/ggml-org/ggml) tensor library as a static
library, installs its headers, and exposes GGML's core tensor-context and
matrix-multiply compute path through `R_RegisterCCallable()` C-callable
entry points. It is **not** a modeling or ops API - the only R-level
surface is `ggml_version()` and an internal smoke-test helper. The point
of the package is for *other* R packages to link against it and drive
GGML tensor math from their own C/C++ code, without each vendoring GGML
themselves.

## What's vendored

A CPU-only, architecture-generic subset of GGML (~5,100 lines across
~35 vendored `.c`/`.cpp`/`.h` files, see `inst/COPYRIGHTS` for the exact
file list and provenance): tensor/context/graph core, the backend
abstraction, tensor-level quantize/dequantize (including K-quants such as
`Q4_K`), and the CPU backend's compute kernels. Built with
`-DGGML_CPU_GENERIC`: no SIMD compiler flags, no OpenMP, no hand-written
x86 SIMD kernel files, no Vulkan/CUDA/Metal/BLAS/etc backends. This is a
deliberate portability choice for a CRAN-facing package - correctness and
buildability everywhere over raw speed. GGML's own architecture-generic
reference kernels still do the compute; they are just the scalar/portable
code path rather than the AVX2/AVX-512/NEON-tuned one.

The GGML sources were vendored via an existing R package,
[`ggmlR`](https://github.com/Zabis13/ggmlR), which already carries the
CRAN-compliance adaptations (stdio/abort redirection) GGML's C code needs
to run safely inside R. See `inst/COPYRIGHTS` for full attribution and
license text (GGML and ggmlR are both MIT; Rggml's own code is
GPL (>= 2)).

**Vulkan** (or any GPU backend) is not built. A future addition would
need: vendoring `ggml-vulkan.*` and the `vulkan-shaders/` sources, a
`glslc`-based shader compilation step in `configure` (present in ggmlR's
own `configure` as a reference), and `libvulkan-dev`/the Vulkan SDK at
build time - none of which is CRAN-portable by default, which is why this
first version is CPU-only.

## C-callable API

Declared in the installed header `inst/include/Rggml.h`, registered from
`src/rggml_init.c`, implemented in `src/rggml_api.c`:

```c
/* version / identity */
int            Rggml_api_version(void);
const char    *Rggml_version(void);

/* context lifecycle */
struct ggml_context *Rggml_context_create(size_t mem_size, int no_alloc);
void                  Rggml_context_free(struct ggml_context *ctx);
size_t                Rggml_used_mem(const struct ggml_context *ctx);
size_t                Rggml_tensor_overhead(void);
size_t                Rggml_graph_overhead(size_t size);

/* tensor creation / introspection (zero-copy: pass a non-NULL `data`) */
struct ggml_tensor *Rggml_new_tensor(struct ggml_context *ctx, enum ggml_type type,
                                      int n_dims, const int64_t *ne, void *data);
void    *Rggml_tensor_data(const struct ggml_tensor *tensor);
void     Rggml_tensor_set_data(struct ggml_tensor *tensor, void *data);
int64_t  Rggml_tensor_ne(const struct ggml_tensor *tensor, int dim);
size_t   Rggml_tensor_nb(const struct ggml_tensor *tensor, int dim);

/* CPU backend */
ggml_backend_t Rggml_backend_cpu_init(void);
void            Rggml_backend_free(ggml_backend_t backend);
int             Rggml_backend_graph_compute(ggml_backend_t backend, struct ggml_cgraph *cgraph);

/* graph building */
struct ggml_cgraph *Rggml_new_graph(struct ggml_context *ctx, size_t size);
void                 Rggml_build_forward_expand(struct ggml_cgraph *cgraph, struct ggml_tensor *tensor);

/* matrix multiply */
struct ggml_tensor *Rggml_mul_mat(struct ggml_context *ctx, struct ggml_tensor *a, struct ggml_tensor *b);
int Rggml_compute_mul_mat(struct ggml_context *ctx, ggml_backend_t backend,
                           struct ggml_tensor *a, struct ggml_tensor *b,
                           float *out_f32, double *out_f64);

/* type/size introspection */
size_t       Rggml_type_size(enum ggml_type type);
size_t       Rggml_row_size(enum ggml_type type, int64_t ne);
int64_t      Rggml_blck_size(enum ggml_type type);
size_t       Rggml_nbytes(const struct ggml_tensor *tensor);
int64_t      Rggml_nelements(const struct ggml_tensor *tensor);
const char  *Rggml_type_name(enum ggml_type type);
```

Every one of these has a `<Name>_ptr()` accessor in `Rggml.h` that
resolves it via `R_GetCCallable()` and returns a function pointer, e.g.
`Rggml_context_create_ptr()` returns an `Rggml_context_create_fun`.

### The `ggml_mul_mat()` convention (verified, not just asserted)

If you load an R matrix directly into a GGML tensor with
`ne = c(nrow(M), ncol(M))` pointing at `M`'s own raw column-major memory
(no copy, no transpose - this is exactly the zero-copy path
`Rggml_new_tensor(..., data)` is for), then:

```
ggml_mul_mat(ctx, A, B)  ==  crossprod(A, B)  ==  t(A) %*% B
```

where the result, read back the same way (dim `c(ncol(A), ncol(B))`), is
always `GGML_TYPE_F32` regardless of the input types. This is exercised in
`inst/tinytest/test_mul_mat.R` with hand-computed small cases and larger
random matrices, through both the ggml-managed and the zero-copy tensor
paths, driven entirely through the *registered* C-callables (via
`.Call()` into `src/rggml_test.c`, which itself only uses the
`Rggml.h`/`R_GetCCallable()` path) - the same path a downstream `LinkingTo`
package would use.

## How a downstream package uses it

```
# DESCRIPTION
Imports: Rggml
LinkingTo: Rggml
```

```c
/* your_package_file.c */
#include <Rggml.h>   /* pulls in ggml.h, ggml-alloc.h, ggml-backend.h, ggml-cpu.h too */

void do_something(void) {
    Rggml_context_create_fun ctx_create = Rggml_context_create_ptr();
    Rggml_backend_cpu_init_fun backend_init = Rggml_backend_cpu_init_ptr();
    Rggml_compute_mul_mat_fun compute_mul_mat = Rggml_compute_mul_mat_ptr();
    /* ... build tensors with struct ggml_tensor *, run ggml_mul_mat, ... */
}
```

No GGML re-vendoring, no linking against `Rggml.so` at the linker level -
only `R_GetCCallable()` symbol resolution at run time (standard R
C-callable convention, same as e.g. `Rfmalloc`/`Rgguf`).

## Status

- `R CMD INSTALL .` - clean, on Linux with a standard R toolchain
  (`gcc`/`g++`, C++17, GNU make).
- `tinytest::test_package("Rggml")` - all tests pass, including the
  end-to-end `ggml_mul_mat()` proof above.
- `OS_type: unix` - only tested on Linux. GGML's own sources are portable
  C/C++ with `#ifdef _WIN32` guards throughout, so a Windows build is
  plausible but not attempted or verified here; see `inst/ggml/Makefile`
  and `configure` if picking this up.
