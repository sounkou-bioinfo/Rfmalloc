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
