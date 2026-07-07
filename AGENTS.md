# AGENTS for Rggml

## Coding standards
- write C as a BSD kernel programmer rather than a Java programmer that failed upwards
- write R as a r-lib programmer rather than a Python programmer that failed upwards

## What this repo is
- `Rggml` is a **carrier package**: it vendors the CPU backend of the
  [GGML](https://github.com/ggml-org/ggml) tensor library as a static library,
  installs its headers, and exposes GGML's core tensor-context and
  matrix-multiply compute path through `R_RegisterCCallable()` C-callable
  entry points.
- It has no high-level R modeling API of its own. The only R surface is
  `ggml_version()` plus an internal smoke-test helper. Downstream packages
  `LinkingTo: Rggml` and drive GGML from their own C/C++ code.
- The vendored subset is CPU-only and architecture-generic
  (`-DGGML_CPU_GENERIC`): correctness/portability over raw speed. No SIMD
  flags, no OpenMP, no Vulkan/CUDA/Metal/BLAS backends.

## First-load instructions
1. Before making package changes, load the package-development guidance in
   `r-package-development`:
   `/root/.pi/agent/git/github.com/sounkou-bioinfo/pi-skills/skills/r-package-development/SKILL.md`.
2. Follow the repo's existing conventions for `R/`, `src/`, `inst/ggml/`,
   `inst/include/`, `man/`, and `inst/tinytest/`.
3. The vendored GGML is built by `./configure` into `inst/ggml/libggml.a`;
   `src/Makevars` (generated) links against it. Do not hand-edit `src/Makevars`.

## Test workflow
- After any code or docs change:
  - `Rscript -e "library(tinytest); test_package('Rggml')"`
- For focused changes:
  - run a single file directly with
    `Rscript -e "source('inst/tinytest/<test_file>.R')"`
- Before finishing a task:
  - `R CMD build . && R CMD check --no-manual Rggml_*.tar.gz`

## `NEWS.md` update policy
- Keep newest entries at the top under `## Development (unreleased)` (or latest
  section). Do not edit historical release notes; append new work to the top.
- One concise, user-visible bullet per behavior change.

## Additional project notes
- `NAMESPACE` and `man/*.Rd` are roxygen-generated; update source roxygen in
  `R/*.R` first, then regenerate.
- Use `tinytest` in `inst/tinytest/` for regression coverage. The end-to-end
  proof is `test_mul_mat.R`, which drives GGML entirely through the *registered*
  C-callables (the same path a `LinkingTo` package would use).
- `OS_type: unix`; keep the build portable across Linux and macOS. GGML's
  sources guard `_WIN32` throughout, so a Windows port is plausible but not
  attempted here.
- Attribution/licensing lives in `inst/COPYRIGHTS` (GGML and ggmlR are MIT;
  Rggml's own code is GPL (>= 2)).
