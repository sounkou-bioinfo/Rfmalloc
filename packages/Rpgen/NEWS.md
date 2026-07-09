# Rpgen 0.1.0 (unreleased)

- Milestone 2: genotype reading, and a first Rfmalloc-backed R surface.
  Two new C-callables, bumping `Rpgen_api_version()` to 2:
  `Rpgen_read_hardcalls()` (`.Call` entry point `RC_rpgen_read_hardcalls`, R
  wrapper `rpgen_read_hardcalls()`) reads a variant range for every sample
  via `plink2::PgrGet()`; `Rpgen_read_dosages()` (`RC_rpgen_read_dosages`,
  `rpgen_read_dosages()`) does the same via `plink2::PgrGetD()`. Both return
  a dense `n_sample x n_variant` matrix (integer 0/1/2/`NA` hardcalls, or
  numeric `[0, 2]`/`NA` dosages). New R-level `rpgen_bed()`/`rpgen_dosage()`
  wrap those readers with `Rfmalloc::fmalloc_bed()`/`Rfmalloc::fmalloc_dosage()`
  to produce fmalloc-backed tensors directly from a `.pgen` file, completing
  the pgen -> standardized tensor -> PCA path (see
  `inst/examples/pgen_pca.R` and `inst/tinytest/test_pgen_pca.R`).
  Verified bit-exact against `pgenlibr::ReadIntList()`/`ReadList()` and
  spot-checked against `pgenlibr::ReadHardcalls()`/`Read()` at biallelic
  variants in `inst/tinytest/test_read.R`, on the same multiallelic-plus-
  dosage fixture `test_open.R` uses. Plain `plink2::PgrGet()`/`PgrGetD()`
  collapse every ALT allele into one non-reference count without needing
  per-variant allele-identity bookkeeping, so - unlike `pgenlibr::NewPgen()`
  at this fixture - neither reader needs a `.pvar`; no scope was cut for the
  multiallelic case.

- Milestone 1: vendored the read subset of PLINK 2's pgenlib (via
  tools/vendor-pgenlib/vendorpgen.R, pinned to CRAN's pgenlibr 0.6.2), built
  it into `libPLINK2.a`, and exposed one C-callable,
  `Rpgen_open_info()` (`.Call` entry point `RC_rpgen_info`, R wrapper
  `rpgen_info()`), which opens a `.pgen` file through
  `PgfiInitPhase1()`/`PgfiInitPhase2()`/`PgrInit()` and reports its sample
  and variant counts. Verified against `pgenlibr::NewPgen()` +
  `GetRawSampleCt()`/`GetVariantCt()` as the oracle in
  `inst/tinytest/test_open.R`, using pgenlibr's own bundled test fixture
  (`chr21_phase3_start.pgen`).
