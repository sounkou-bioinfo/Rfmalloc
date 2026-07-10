# RfmallocStatgen 0.1.0 (unreleased)

- Package scaffold: `RfmallocStatgen` joins the Rfmalloc monorepo as the
  statistical-genetics layer (see `ROADMAP.md` / the README for the full
  plan). This first commit ships the package skeleton and one working
  method, so the package builds and checks green from the start.
- `statgen_gwas_lin()`: univariate linear-regression GWAS over a `bed` or
  `dosage` fmalloc genotype tensor, the fused whole-genome form (residualize
  once on the covariate design, then reduce per variant), not a loop of
  per-variant `lm()` calls. Missing genotype calls are mean-imputed per
  variant. `inst/tinytest/test_gwas_lin.R` checks `beta`/`se`/`t`/`p` for
  every variant against `lm(y ~ covar + g_j)` to floating-point tolerance,
  and cross-checks against `bigstatsr::big_univLinReg()` when `bigstatsr` is
  installed.
- `statgen_pca()`: fast principal-component analysis by randomized SVD. The
  RSVD workhorse is vendored verbatim (GPL-3) from 'PCAone'
  (`src/pcaone/`, `RsvdOpData::computeUSV`), not reimplemented, behind the
  pinned drift-guard recipe `tools/vendor-pcaone/vendorpcaone.R` (CI job
  `vendored-pcaone-matches-recipe`). This makes the package compiled for the
  first time (`LinkingTo: RcppEigen`, C++17). Stage 1 drives the vendored
  core over an in-memory matrix; `inst/tinytest/test_pca.R` matches
  `base::svd()`, `stats::prcomp()`, and `pcaone::pcaone()` to ~1e-6 (in
  practice machine precision on a decaying spectrum).
- `statgen_pca()` now also takes a 2-D `bed`/`dosage` fmalloc genotype tensor
  and decomposes it **out of core**: the tensor is streamed one variant-column
  block at a time through Rfmalloc's fused-standardize decode (the new
  `Rfmalloc_tensor_decode` C-callable, API v7) and the dense samples x variants
  matrix is never materialized. To get a standardized PCA, standardize the
  tensor first (`fmalloc_bed_standardize()` / `fmalloc_dosage_standardize()`) -
  standardization rides the decode. `inst/tinytest/test_pca_ooc.R` checks that
  the out-of-core result equals the in-core path on the same standardized
  tensor to floating point, independent of the streaming block size, for both
  `bed` and `dosage`.
- `statgen_coloc_bf()`: all-pairs Bayesian colocalisation of two traits'
  signals, coloc's `coloc.bf_bf` recast as a single matrix multiply. Stacking
  each trait's per-SNP log approximate Bayes factors as a matrix of signals,
  the shared-causal (H4) coupling for every signal pair is one stabilized
  log-sum-exp GEMM (`c = outer(m1,m2,"+") + log(E1 %*% t(E2))`); the coloc
  posteriors PP0..PP4 follow elementwise. The hot kernel therefore rides any
  backend 'Rggml' offers via `backend = "cpu"` / `"blas"` / `"vulkan"` -
  including a Vulkan GPU - and larger-than-memory signal sets via the
  out-of-core panel-GEMM; this is the same idea as gpu-coloc (Torch) without
  its CUDA/Metal-only lock-in. `trim = TRUE` returns the per-pair posterior
  overlap and flags low-overlap H4 (the analogue of `trim_by_posterior`).
  `inst/tinytest/test_coloc_bf.R` proves the matrix form is identical to the
  consecutive per-pair form to machine precision, that the single-precision
  backend agrees with the double-precision CPU path, and that it recovers
  colocalisation vs distinct causals.
