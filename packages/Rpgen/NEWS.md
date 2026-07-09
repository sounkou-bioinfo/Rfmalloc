# Rpgen 0.1.0 (unreleased)

- Milestone 4a: `rpgen_import_vcf()` converts a VCF straight to a `.pgen` by
  calling plink2's own `VcfToPgen()` importer - reusing plink2's import code
  rather than a from-scratch `htslib`-based reader, since the same vendored
  closure also covers BCF/BGEN/Oxford for a later milestone. A second
  vendoring recipe, `tools/vendor-plink2-import/`, pins plink-ng at commit
  `a81e38220b16e3907bdcedbe6ce39b273e001e13` and adds the program-level files
  `VcfToPgen()` needs (verified by actually compiling and linking the
  closure, not just following `#include`s) plus two library-level additions
  (`pgenlib_write.cc`, the `.pgen` writer; `SFMT`, the PRNG
  `plink2_random.h` wraps). One local patch routes `plink2_cmdline.cc`'s
  logging through `Rprintf()`/`REprintf()` instead of a log `FILE*`/stdout/
  stderr - required, not cosmetic, since Rpgen never opens a log file and
  upstream's version would segfault on it. New C-callable
  `Rpgen_import_vcf()` (`.Call` entry point `RC_rpgen_import_vcf`), bumping
  `Rpgen_api_version()` to 4; new R-level `rpgen_import_vcf()` and the
  convenience `rpgen_import_bed()` (import straight into an `Rfmalloc` `bed`
  tensor, matching `rpgen_bed()`). `inst/tinytest/test_import_vcf.R` writes
  a tiny VCF with known `GT` fields, converts it, and asserts the genotypes
  read back through the existing (unmodified) `rpgen_read_hardcalls()`
  match exactly. Known gap, tracked for follow-up rather than blocking this
  milestone: `R CMD check` reports one WARNING for raw
  `printf()`/`stdout`/`stderr` calls scattered through the ~18,000-line
  vendored `plink2_import.cc` outside the patched logging functions (mostly
  a `\r`-progress spinner); a handful of `exit()` calls likewise remain in
  paths our driver's fixed defaults never reach (multiallelic dosage import,
  multiallelic variant split/join) but would need patching before a future
  milestone enables those options.

- Milestone 3: a native PLINK 1 `.bed` reader, bumping `Rpgen_api_version()`
  to 3. `PgfiInitPhase1()` already opens a PLINK 1 `.bed` transparently, in
  the same code path as a `.pgen` (its `vrtypes` simply come back `NULL`);
  the one real difference is that a `.bed` carries no header, so its
  sample/variant counts have to come from the companion `.fam`/`.bim` line
  counts instead and are passed in explicitly. New C-callable
  `Rpgen_read_bed_hardcalls()` (`.Call` entry point
  `RC_rpgen_read_bed_hardcalls`) reads every variant, for every sample, via
  the same `plink2::PgrGet()` loop `Rpgen_read_hardcalls()` uses - a `.bed`
  is biallelic hardcalls only, so there is no dosage counterpart. New
  R-level `rpgen_bed_info()` (counts from `.bim`/`.fam`) and
  `rpgen_read_bed_hardcalls()` (`bed`, `bim = NULL`, `fam = NULL`, defaulting
  the companions to `bed` with its extension swapped); `rpgen_bed()` now
  dispatches to this reader whenever `path` ends in `.bed`, keeping its
  `.pgen` behavior otherwise. There is no plink2 CLI or `.bed` fixture
  available to vendor, so `inst/tinytest/test_bed.R` generates one by hand
  (magic bytes, 2-bit SNP-major packing) from a small known genotype matrix
  and asserts an exact round trip; cross-checked against
  `pgenlibr::NewPgen()`/`ReadIntList()` on the same generated file when
  `pgenlibr` is installed.

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
