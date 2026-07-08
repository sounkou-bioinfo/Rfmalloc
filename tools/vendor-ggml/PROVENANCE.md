# Vendored GGML — provenance & update policy

`packages/Rggml/inst/ggml` (and the public headers in
`packages/Rggml/inst/include`) are **generated**, not hand-maintained. They are
produced entirely by [`vendorggml.R`](vendorggml.R) from three pinned inputs:

```
inst/ggml  =  (SHA-pinned ggmlR CRAN tarball)  +  patches/  +  overlay/
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

## patches/ — our 9 local edits, on top of stock 0.7.8

| patch | rationale |
|---|---|
| `ggml-backend.cpp`, `ggml-backend-impl.h`, `ggml-alloc.c` | pass `ggml_backend_buffer_i` **by pointer**, not by value, across the `extern "C"` TU boundary — passing the ~88-byte POD by value silently crashed on Windows/MinGW. |
| `ggml-backend-meta.cpp` | make the per-device meta caches **never-destroyed heap singletons**, so C++ does not run their destructors at process exit in an order undefined relative to backend/driver teardown (faulted otherwise). |
| `ggml-context.c`, `ggml-graph.c` | **NULL-guard** the results of `ggml_new_object` / `ggml_new_graph_custom`: on a too-small memory pool these return NULL, and the stock code dereferenced them → heap corruption / silent abort. |
| `ggml-cpu/arch-fallback.h` | drop the `ggml_vec_dot_q4_K_q8_K_generic → ...` alias so our **runtime SIMD dispatcher** (`packages/Rggml/tools/simd/`) can own the canonical symbol and route to the staged AVX2/NEON variant. |
| `ggml-vulkan/ggml-vulkan-graph.cpp` | one call site passes the buffer interface **by value**; our by-pointer patch above changes that signature, so it needs the `&`. (The other `ggml-vulkan-*.cpp` are `#include`d into `ggml-vulkan.cpp`, which is the only Vulkan TU compiled on its own.) |
| `ggml-vulkan/ggml-vulkan-misc.cpp` | **`GGML_VK_ALLOW_CPU=1`** opts in to Vulkan devices reporting `VK_PHYSICAL_DEVICE_TYPE_CPU`. Upstream deliberately refuses them, so a software driver (Mesa lavapipe) enumerates nothing and the Vulkan backend cannot be correctness-tested on a machine without a GPU — including CI. Unset, behaviour is byte-for-byte upstream's. |

## overlay/ — files that are ours, not GGML's

`Makefile` (the CPU-backend build with our DEFS, the BLAS backend, and the SIMD
staging rules), `ggml-blas.cpp` + `ggml-blas.h` (GGML's BLAS backend, taken
directly from ggml-org), `cblas.h` + `rggml_cblas.c` (the portable
`cblas_sgemm`→Fortran `sgemm_` shim), and `rggml_compat.h` + `rggml_io.c` (the
R-safe stdio/abort I/O shim). These are copied in verbatim; they are versioned
here, not fetched.

## Update policy

The vendored core stays at GGML **0.9.5** until there is a concrete reason to
move — the next one being the **CUDA backend**. At that point GGML will be
refreshed *directly from ggml-org / llama.cpp in-tree* at a pinned commit SHA
(so the core version-matches the CUDA sources and is testable on real
hardware), and these patches re-applied on top. `vendorggml.R` is the template
for that: swap the pinned base, re-run, re-validate.

## GPU path

The `ggml-vulkan/` tree (backend + 156 `.comp` shaders + the shader generator)
**is vendored** — it comes from the same pinned tarball as the CPU core, so it
version-matches it, and it is listed in `manifest.txt` like everything else. It
is compiled only when `packages/Rggml/configure` is given `--with-vulkan`
(never auto-detected: the largest generated shader TU is a 141 MB array literal
that needs ~5 GB of RAM to compile, independent of `-O` level).

CUDA is the remaining GPU route (NVIDIA-only, must come direct from ggml-org at
a version matching the core). When it lands it goes in the same way: add its
sources to `manifest.txt`, its build to `configure`, and any local edits to
`patches/`.
