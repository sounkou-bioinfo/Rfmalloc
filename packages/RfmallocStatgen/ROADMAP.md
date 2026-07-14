# RfmallocStatgen research programme

RfmallocStatgen closes statistical-genetics algorithms over the smallest typed
store each one needs. Dense genotype matrices and dense LD matrices are not the
universal intermediate.

## Implemented

- Genotype tensor methods: linear GWAS in `statgen_gwas_lin()`, streamed
  randomized-SVD PCA in `statgen_pca()`, and banded LD construction in
  `statgen_snp_cor()`.
- LD-store methods: `statgen_ldpred2_inf()`, `statgen_ldpred2()`, and
  `statgen_ldpred2_auto()` read neighbour runs from `fmalloc_ld` rather than a
  dense variant-by-variant matrix.
- Signal-matrix methods: `statgen_coloc_bf()` casts every signal-pair coupling
  as one matrix multiply and can use any registered backend.

Direct dense references and optional bigsnpr/bigstatsr comparisons pin the
numerical contracts. PCA's randomized-SVD core is pinned from GPL-3 PCAone;
the other methods are clean-room implementations of their published
algorithms.

## Open contradictions

- Genotype touching: logistic regression, LD clumping and pruning, GRM,
  iterative LD-region removal, autoSVD, and PCA projection.
- LD only: lassosum2, LD score regression, and split-LD.
- Phase aware: direct locus-row access for Li and Stephens local ancestry,
  chromosome painting, IBD, and kalis-class forward-backward kernels.
- Compute: keep GPU work behind the backend decline contract. The current WSL
  rig exposes the RTX 5050 through CUDA only, so a real Vulkan measurement
  needs a native-Linux Vulkan-visible device.
