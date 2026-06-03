# AGENTS for Rfmalloc

## Coding standards

- write C as a BSD kernel programmer rather than a Java programmer that
  failed upwards
- write R as a r-lib programmer rather than a Python programmer that
  failed upwards

## What this repo is

- `Rfmalloc` is an R package providing file-backed ALTREP vector types
  powered by a bundled `fmalloc` allocator runtime.
- The package supports persistent and scratch runtimes, ALTREP
  serialization, and an explicit native/R destroy path via
  [`destroy_fmalloc_vector()`](https://sounkou-bioinfo.github.io/Rfmalloc/reference/destroy_fmalloc_vector.md).

## First-load instructions

1.  Before making package changes, load the package-development guidance
    in `r-package-development`:
    `/root/.pi/agent/git/github.com/sounkou-bioinfo/pi-skills/skills/r-package-development/SKILL.md`.
2.  Follow the repo’s existing conventions for `R/`, `src/`, `man/`, and
    `inst/tinytest/`.

## Test workflow

- After any code or docs change:
  - `Rscript -e "library(tinytest); test_package('Rfmalloc')"`
- For focused changes:
  - run a single file directly with
    `Rscript -e "source('inst/tinytest/<test_file>.R')"`
- Before finishing a task:
  - `R CMD check --no-manual .`
- Use small helper targets from `Makefile` when present if they already
  encode the same checks.

## `NEWS.md` update policy

- Keep newest entries at the top under `## Development (unreleased)` (or
  latest section).
- Add concise, user-visible bullets grouped by feature/fix area.
- Prefer one bullet per behavior change (e.g. persistence/unserialize
  semantics, destroy/recovery behavior, diagnostics).
- Do not edit historical release notes; append new work to the top
  section.

## Additional project notes

- `NAMESPACE` and `man/*.Rd` are roxygen-generated in this repo; update
  source roxygen in `R/*.R` first, then regenerate if needed.
- Use `tinytest` in `inst/tinytest/` for regression coverage.
- Keep temporary files under
  [`tempfile()`](https://rdrr.io/r/base/tempfile.html)/[`tempdir()`](https://rdrr.io/r/base/tempfile.html)
  and clean them explicitly.
