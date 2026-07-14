# AGENTS for Rggml

## Coding standards

- write C as a BSD kernel programmer rather than a Java programmer that
  failed upwards
- write R as a r-lib programmer rather than a Python programmer that
  failed upwards

## What this repo is

- `Rggml` is a low-level **carrier package**. Its generated static
  library contains GGML’s core, official GGUF implementation, CPU and
  BLAS backends, and the opt-in Vulkan backend. Sibling packages consume
  these through `R_RegisterCCallable()` rather than re-vendoring GGML.
- Model composition belongs in Rllm and the R-facing GGUF storage layer
  belongs in Rgguf. Rggml’s small R surface provides diagnostics and
  direct matrix multiplication.
- On x86, selected kernels are staged with ISA flags by `configure` and
  chosen by runtime dispatch. On aarch64, GGML’s NEON kernels are the
  baseline. ISA flags must never appear in R’s recorded package flags.

## First-load instructions

1.  Before making package changes, load the package-development guidance
    in `r-package-development`:
    `/root/.pi/agent/git/github.com/sounkou-bioinfo/pi-skills/skills/r-package-development/SKILL.md`.
2.  Follow the repo’s existing conventions for `R/`, `src/`,
    `inst/ggml/`, `inst/include/`, `man/`, and `inst/tinytest/`.
3.  The vendored GGML is built by `./configure` into
    `inst/ggml/libggml.a`; `src/Makevars` (generated) links against it.
    Do not hand-edit `src/Makevars`.
4.  Never hand-edit `inst/ggml`. Change
    `tools/vendor-ggml/manifest.txt`, a patch, or an overlay file and
    run `vendorggml.R vendor`.

## Test workflow

- After any code or docs change:
  - `Rscript -e "library(tinytest); test_package('Rggml')"`
- For focused changes:
  - run a single file directly with
    `Rscript -e "source('inst/tinytest/<test_file>.R')"`
- Before finishing a task:
  - `R CMD build . && R CMD check --no-manual Rggml_*.tar.gz`

## `NEWS.md` update policy

- Keep newest entries at the top under `## Development (unreleased)` (or
  latest section). Do not edit historical release notes; append new work
  to the top.
- One concise, user-visible bullet per behavior change.

## Additional project notes

- `NAMESPACE` and `man/*.Rd` are roxygen-generated; update source
  roxygen in `R/*.R` first, then regenerate.
- Use `tinytest` in `inst/tinytest/` for regression coverage. The
  end-to-end proof is `test_mul_mat.R`, which drives GGML entirely
  through the *registered* C-callables (the same path a `LinkingTo`
  package would use).
- Linux, macOS, and Windows are supported. `configure.win` reuses the
  main configure path; keep platform-specific behavior there rather than
  forking the build recipe.
- Attribution/licensing lives in `inst/COPYRIGHTS` (GGML and ggmlR are
  MIT; Rggml’s own code is GPL (\>= 2)).
