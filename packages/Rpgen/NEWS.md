# Rpgen 0.1.0 (unreleased)

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
