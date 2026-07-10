# RfmallocStatgen roadmap

RfmallocStatgen is the statistical-genetics layer of the Rfmalloc stack: it
runs genome-scale methods over fmalloc-backed genotype tensors, closing over
bigsnpr's analysis stack on denser storage with native readers, and adding
the haplotype methods bigsnpr cannot.

Every method is validated against bigsnpr/bigstatsr as the reference oracle
(clean-room from the published algorithms; both are GPL-3, so we implement
the methods, never lift code).

**Tier 1: genotype-touching** (over `bed`/`dosage` fmalloc tensors)

- Univariate GWAS regression, linear and logistic (bigstatsr
  `big_univLinReg`/`big_univLogReg`). [linear: SHIPPED as the first method,
  `statgen_gwas_lin()`]
- `snp_cor`: windowed LD correlation into a banded LD matrix (Rfmalloc `ld`
  codec). [SHIPPED as `statgen_snp_cor()`: streams the genotype tensor one
  variant column at a time, packs the banded correlations into an `fmalloc_ld`
  store; validated against `stats::cor()` and `bigsnpr::snp_cor()`]
- LD clumping and pruning.
- Genetic relatedness matrix (GRM) / `tcrossprodSelf`.
- Randomized-SVD PCA with iterative LD-region removal (`autoSVD`), PCA
  projection.

**Tier 2: LD-matrix-only** (storage-agnostic sparse linear algebra over the
LD matrix)

- LDpred2 and LDpred2-auto (Gibbs sampling over the sparse LD matrix + GWAS
  `beta_hat`).
- lassosum2 (coordinate descent).
- LD score regression (heritability).
- `split-LD`: min-cost dynamic-programming partition of the genome into LD
  blocks.

**Haplotype methods** (additive; bigsnpr has none)

- Li and Stephens HMM local ancestry (kalis-class, over
  `fmalloc_haplotypes`).
- Chromosome painting, IBD, phasing-aware analyses.
- A fork of kalis reading directly from `fmalloc_haplotypes` (and eventually
  GPU-accelerating the Li and Stephens forward-backward) is a candidate,
  since plink2's Oxford .haps import gives a lean phased-haplotype input
  path.

**GPU**: Rfmalloc's own portable, vendor-neutral GPU composition (Rggml's
Vulkan backend + the device-buffer residency API) already serves the
genotype GEMM (`crossprod`/`%*%`); it runs on any vendor's GPU, not just
NVIDIA. Benchmarking a real GPU number needs a Vulkan-visible discrete GPU (a
native-Linux vendor driver); the WSL rig exposes only llvmpipe to Vulkan, so
its RTX 5050 is CUDA-only there.
