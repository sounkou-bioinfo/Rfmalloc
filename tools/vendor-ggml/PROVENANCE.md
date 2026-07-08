# Vendored GGML - provenance & update policy

`packages/Rggml/inst/ggml` (and the public headers in
`packages/Rggml/inst/include`) are **generated**, not hand-maintained. They are
produced entirely by [`vendorggml.R`](vendorggml.R) from four pinned inputs:

```
inst/ggml  =  (SHA-pinned ggmlR CRAN tarball)      # the core + Vulkan
           +  (SHA-pinned ggml-org arch/arm/quants.c)   # the NEON kernels
           +  patches/
           +  overlay/
```

Re-run and verify at any time:

```sh
Rscript tools/vendor-ggml/vendorggml.R check     # assert the committed tree == this recipe
Rscript tools/vendor-ggml/vendorggml.R vendor    # regenerate in place
```

This is what it means to **own our path to GGML**: the tree does not depend on
a git repository that rewrites its own history. The source of record is an
immutable CRAN artifact plus a small, documented patch set, both kept here.

## Base

- **ggmlR `0.7.8`** source tarball from CRAN
  (`ggmlR_0.7.8.tar.gz`, sha256 `f7f414729e389dce7320cfcfd5c63298382da00c436e3e5bc49bf33f067d0dc7`),
  pinned in `vendorggml.R`. CRAN source tarballs are immutable and permanently
  archived, so this pin can never shift under us.
- ggmlR is a CRAN-maintained package by Yuri Baramykov that vendors **GGML
  v0.9.5**, already split into CRAN-digestible translation units and carrying
  the stdio/abort compliance shim GGML's C needs to run safely inside R. Its
  *git* history is not usable for pinning (the maintainer resets/squashes the
  repo on each release); its *CRAN tarballs* are. We vendor from the tarball.
- `manifest.txt` lists the 52 GGML files we compile (the CPU backend subset);
  ggmlR 0.7.8 also ships `ggml-opt`, `onnx/`, x86 arch kernels, and a full
  `ggml-vulkan/` tree that we do **not** copy (see "GPU path" below).
- 45 of those 52 files are byte-identical to stock ggmlR 0.7.8; the other 7 are
  our patches below.

## Second base: the ARM kernels

ggmlR prunes `ggml-cpu/arch/`, so its quantized `vec_dot`s are the portable
scalar reference on every machine. On aarch64 that leaves real performance on
the floor for no portability gain, because NEON is a *mandatory* part of the
architecture: the kernels need no ISA flag, and a build cannot emit an
instruction the host lacks.

- **`ggml-cpu/arch/arm/quants.c`** from **ggml-org/ggml `v0.9.11`**, pinned by
  the **file's** sha256 (`0efbb89f…`) rather than the archive's - GitHub has
  changed its tarball compression before, so archive checksums are not stable
  pins, while a blob's content is. It is *not* in `manifest.txt`, which lists
  what we take from the ggmlR tarball.
- Only `quants.c` is taken. `arch/arm/repack.cpp` calls `_generic` helpers that
  do not exist in 0.9.5, so the portable repack stays.
- `arch/x86/` is deliberately **not** vendored: its kernels are selected by
  compile-time `#if defined(__AVX2__)`, so building them means shipping a
  binary that faults on pre-AVX2 hardware. That is what our runtime SIMD
  dispatcher (`packages/Rggml/tools/simd/`) exists to avoid, and what forces
  GGML upstream into `GGML_CPU_ALL_VARIANTS` + `dlopen`.

## patches/ - our 9 local edits, on top of stock 0.7.8

| patch | rationale |
|---|---|
| `ggml-backend.cpp`, `ggml-backend-impl.h`, `ggml-alloc.c` | pass `ggml_backend_buffer_i` **by pointer**, not by value, across the `extern "C"` TU boundary - passing the ~88-byte POD by value silently crashed on Windows/MinGW. |
| `ggml-backend-meta.cpp` | make the per-device meta caches **never-destroyed heap singletons**, so C++ does not run their destructors at process exit in an order undefined relative to backend/driver teardown (faulted otherwise). |
| `ggml-context.c`, `ggml-graph.c` | **NULL-guard** the results of `ggml_new_object` / `ggml_new_graph_custom`: on a too-small memory pool these return NULL, and the stock code dereferenced them → heap corruption / silent abort. |
| `ggml-cpu/arch-fallback.h` | two edits. Drop the `ggml_vec_dot_q4_K_q8_K_generic → ...` alias so our **runtime SIMD dispatcher** (`packages/Rggml/tools/simd/`) can own the canonical symbol and route to the staged AVX2 variant. And add **20 aarch64 aliases**: `arch/arm/quants.c` implements 23 canonical `vec_dot`s but not the repack GEMMs, nor ggmlR's custom `q1_0` type, so those must still resolve to the generic code. Verified by building the full aarch64 object set: 0 unresolved GGML symbols. |
| `ggml-vulkan/ggml-vulkan-graph.cpp` | one call site passes the buffer interface **by value**; our by-pointer patch above changes that signature, so it needs the `&`. (The other `ggml-vulkan-*.cpp` are `#include`d into `ggml-vulkan.cpp`, which is the only Vulkan TU compiled on its own.) |
| `ggml-vulkan/ggml-vulkan-misc.cpp` | **`GGML_VK_ALLOW_CPU=1`** opts in to Vulkan devices reporting `VK_PHYSICAL_DEVICE_TYPE_CPU`. Upstream deliberately refuses them, so a software driver (Mesa lavapipe) enumerates nothing and the Vulkan backend cannot be correctness-tested on a machine without a GPU - including CI. Unset, behaviour is byte-for-byte upstream's. |

## overlay/ - files that are ours, not GGML's

`Makefile` (the CPU-backend build with our DEFS, the BLAS backend, and the SIMD
staging rules), `ggml-blas.cpp` + `ggml-blas.h` (GGML's BLAS backend, taken
directly from ggml-org), `cblas.h` + `rggml_cblas.c` (the portable
`cblas_sgemm`→Fortran `sgemm_` shim), and `rggml_compat.h` + `rggml_io.c` (the
R-safe stdio/abort I/O shim). These are copied in verbatim; they are versioned
here, not fetched.

## Update policy

The vendored core stays at GGML **0.9.5** until there is a concrete reason to
move. `vendorggml.R` is the template for whenever that happens: swap the pinned
base, re-run, re-validate.

**Vulkan is the GPU backend, not a stepping stone to CUDA.** It is already
vendored, version-matched to the core, and portable across AMD, Intel, NVIDIA
and (via MoltenVK) Apple. CUDA buys NVIDIA-only hardware in exchange for `nvcc`,
an arch-flag matrix, a multi-GB toolchain no CRAN builder has, and a second
GPU code path to keep correct. It is worth adding only for measured throughput
on NVIDIA that Vulkan cannot reach, and it would then be an *additional* opt-in
backend behind the same device-buffer residency API the Vulkan work already
built, not a replacement. That API (`Rggml_backend_alloc_ctx_tensors`,
`_tensor_set`, `_tensor_get`) is what makes the two interchangeable.

## GPU path

The `ggml-vulkan/` tree (backend + 156 `.comp` shaders + the shader generator)
**is vendored** - it comes from the same pinned tarball as the CPU core, so it
version-matches it, and it is listed in `manifest.txt` like everything else. It
is compiled only when `packages/Rggml/configure` is given `--with-vulkan`
(never auto-detected: the largest generated shader TU is a 141 MB array literal
that needs ~5 GB of RAM to compile, independent of `-O` level).

If CUDA is ever added it goes in the same way: its sources into `manifest.txt`,
its build into `configure`, any local edits into `patches/`. See the update
policy above for why it is not the default GPU route.
