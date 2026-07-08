# AGENTS for the Rfmalloc monorepo

One repo, four R packages, one story: **out-of-core array computation over
pluggable storage codecs and compute backends**. fmalloc (mmap storage), ALP
(lossless float compression), GGUF quantized blocks, and GGML kernels are each
*one bet*; the packages/<pkg> layout exists so a change that spans bets lands
as one commit and is validated as one unit.

## Coding standards
- write C as a BSD kernel programmer rather than a Java programmer that failed upwards
- write R as a r-lib programmer rather than a Python programmer that failed upwards
- license is GPL (>= 2) everywhere; vendored MIT/BSD components are documented
  in each package's `inst/COPYRIGHTS` (never relicense, never vendor without
  updating COPYRIGHTS)

## Layout
- `packages/Rfmalloc` - substrate: file-backed ALTREP + fmalloc runtime, codec
  registry + matmul backend registry (C API, see `inst/include/Rfmalloc.h`),
  panel matmul, OOC eviction, ALP/sparse codecs
- `packages/Rgguf` - GGUF reader/writer; registers the quantized codecs with
  Rfmalloc; vendored gguflib (with local fixes - see its COPYRIGHTS)
- `packages/Rggml` - vendored GGML static lib + C-callables; BLAS backend via
  cblas→Fortran shim; opt-in Vulkan backend (`--with-vulkan`); on x86,
  runtime-SIMD kernels staged in `tools/simd` by configure (RsimdDispatch
  pattern: ISA flags never appear in R's recorded flags); on aarch64, GGML's
  own NEON kernels instead (mandatory baseline, no dispatch needed). The
  vendored tree is **generated** by `tools/vendor-ggml/vendorggml.R` from
  pinned sources: never hand-edit `inst/ggml`, edit a patch and re-run
  `vendorggml.R vendor`. CI enforces this (`vendored-ggml-matches-recipe`).
- `packages/Rllm` - composition: registers Rggml as Rfmalloc's typed
  (codec-aware) matmul backend; quantize/dequantize R surface; the LLM
  graph-builder lands here

## Cross-package rules (the reason this is a monorepo)
- The C-callable APIs are contracts: bump `RFMALLOC_API_VERSION` /
  `RGGML_API_VERSION` when you extend them, and update every consumer in the
  same commit.
- Correctness against references, not intuition: quantized decode paths must
  stay bit-identical to GGML's `to_float` (`Rggml_dequantize`). The pins are
  `packages/Rgguf/inst/tinytest/test_gguf_codec_ggml_ref.R` (fixture) and
  `packages/Rllm/inst/tinytest/test_codec_consistency.R` (live). If you touch
  a codec, run both.
- Any backend may decline any product (return non-zero); Rfmalloc's decode+BLAS
  fallback must always produce correct results. Never make correctness depend
  on which backend is selected.

## Test workflow
- Per package: `Rscript -e "tinytest::test_package('<pkg>')"` (install the
  package and its local siblings first: `R CMD INSTALL packages/<sibling>`
  in dependency order Rfmalloc, Rggml, Rgguf, Rllm).
- Focused: `Rscript -e "tinytest::run_test_file('packages/<pkg>/inst/tinytest/<file>.R')"`.
- Before finishing: `R CMD check --no-manual packages/<pkg>` for every package
  you touched; CI additionally runs the full-stack integration job.

## Conventions
- `NAMESPACE` and `man/*.Rd` are roxygen2-generated; edit roxygen comments,
  then `Rscript -e "roxygen2::roxygenise('packages/<pkg>')"`.
- `NEWS.md` per package, newest entries on top; one bullet per user-visible
  change.
- Tests are tinytest, in `packages/<pkg>/inst/tinytest/`.
- READMEs: `README.Rmd` is the source where present; re-render to `README.md`.
- CI is `.github/workflows/R-CMD-check.yaml` (per-package matrix + integration
  job). Keep it green on every push. Rfmalloc and Rggml also build on Windows;
  all four are checked on aarch64 (`ubuntu-24.04-arm`, macOS Apple Silicon).
- Prose is written without em dashes, anywhere: READMEs, DESCRIPTION, NEWS,
  commit messages. Use a colon in a title, ` - ` mid-sentence.
