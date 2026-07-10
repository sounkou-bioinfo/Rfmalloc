
# RfmallocStatgen

<!-- badges: start -->

[![R-CMD-check](https://github.com/sounkou-bioinfo/Rfmalloc/actions/workflows/R-CMD-check.yaml/badge.svg)](https://github.com/sounkou-bioinfo/Rfmalloc/actions/workflows/R-CMD-check.yaml)
<!-- badges: end -->

RfmallocStatgen is the statistical-genetics layer of the
[Rfmalloc](https://github.com/sounkou-bioinfo/Rfmalloc) stack: it runs
genome-scale methods over fmalloc-backed genotype tensors (2-bit `bed`,
1-byte `dosage`), closing over the analysis stack popularized by
[bigsnpr](https://privefl.github.io/bigsnpr/)/[bigstatsr](https://privefl.github.io/bigstatsr/)
on denser storage with native readers
([Rpgen](https://github.com/sounkou-bioinfo/Rfmalloc): pgen/bed/VCF),
and adding the haplotype methods bigsnpr cannot express.

## Roadmap

RfmallocStatgen is the statistical-genetics layer of the Rfmalloc stack:
it runs genome-scale methods over fmalloc-backed genotype tensors,
closing over bigsnpr’s analysis stack on denser storage with native
readers, and adding the haplotype methods bigsnpr cannot.

Every method is validated against bigsnpr/bigstatsr as the reference
oracle (clean-room from the published algorithms; both are GPL-3, so we
implement the methods, never lift code).

**Tier 1: genotype-touching** (over `bed`/`dosage` fmalloc tensors)

- Univariate GWAS regression, linear and logistic (bigstatsr
  `big_univLinReg`/`big_univLogReg`). \[linear: SHIPPED as the first
  method, `statgen_gwas_lin()`\]
- `snp_cor`: windowed LD correlation into a sparse LD matrix (Rfmalloc
  `sparse` codec).
- LD clumping and pruning.
- Genetic relatedness matrix (GRM) / `tcrossprodSelf`.
- Randomized-SVD PCA with iterative LD-region removal (`autoSVD`), PCA
  projection.

**Tier 2: LD-matrix-only** (storage-agnostic sparse linear algebra over
the LD matrix)

- LDpred2 and LDpred2-auto (Gibbs sampling over the sparse LD matrix +
  GWAS `beta_hat`).
- lassosum2 (coordinate descent).
- LD score regression (heritability).
- `split-LD`: min-cost dynamic-programming partition of the genome into
  LD blocks.

**Haplotype methods** (additive; bigsnpr has none)

- Li and Stephens HMM local ancestry (kalis-class, over
  `fmalloc_haplotypes`).
- Chromosome painting, IBD, phasing-aware analyses.
- A fork of kalis reading directly from `fmalloc_haplotypes` (and
  eventually GPU-accelerating the Li and Stephens forward-backward) is a
  candidate, since plink2’s Oxford .haps import gives a lean
  phased-haplotype input path.

**GPU**: Rfmalloc’s own portable, vendor-neutral GPU composition
(Rggml’s Vulkan backend + the device-buffer residency API) already
serves the genotype GEMM (`crossprod`/`%*%`); it runs on any vendor’s
GPU, not just NVIDIA. Benchmarking a real GPU number needs a
Vulkan-visible discrete GPU (a native-Linux vendor driver); the WSL rig
exposes only llvmpipe to Vulkan, so its RTX 5050 is CUDA-only there.

See `ROADMAP.md` in the package source for this same plan outside the
rendered README.

## Usage

`statgen_gwas_lin()` regresses a phenotype on every variant in a
`bed`/`dosage` fmalloc genotype tensor, fused across variants
(residualize once on the covariate design, then reduce per variant), not
a loop of per-variant `lm()` calls. This toy example plants one causal
variant and checks the fitted effect against `lm()` directly:

``` r
rt <- open_fmalloc(tempfile(fileext = ".bin"), mode = "scratch", size_gb = 0.1)

set.seed(1)
n <- 300L
m <- 20L
g <- matrix(sample(0:2, n * m, replace = TRUE, prob = c(0.25, 0.5, 0.25)),
            nrow = n, ncol = m)
storage.mode(g) <- "integer"
covar <- cbind(age = rnorm(n, 50, 10), sex = rbinom(n, 1, 0.5))
causal <- 5L
y <- 0.8 * g[, causal] + 0.05 * covar[, "age"] + 0.3 * covar[, "sex"] + rnorm(n)

tn <- fmalloc_bed(g, runtime = rt)
gwas <- statgen_gwas_lin(tn, y, covar = covar)

gwas[causal, ]
#>        beta         se        t           p   n
#> 5 0.9237098 0.08957976 10.31159 1.67524e-21 300

# Cross-checked against a direct lm() fit at the causal variant.
fit <- lm(y ~ covar[, "age"] + covar[, "sex"] + g[, causal])
coef(summary(fit))[4L, ]
#>     Estimate   Std. Error      t value     Pr(>|t|) 
#> 9.237098e-01 8.957976e-02 1.031159e+01 1.675240e-21

cleanup_fmalloc(rt)
```

The `beta`/`se`/`t`/`p` columns match `lm()`’s coefficient row for every
variant, not just the causal one; see `inst/tinytest/test_gwas_lin.R`.

## Provenance and license

RfmallocStatgen is GPL-3, matching bigsnpr and bigstatsr (both GPL-3):
no code from either is vendored, only their published algorithms and
their output as a numerical oracle for this package’s own tests.
