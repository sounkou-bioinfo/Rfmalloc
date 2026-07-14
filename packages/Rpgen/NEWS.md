# Rpgen 0.1.0 (unreleased)

- Removed the unused C-callable API counter. Rpgen and every native consumer
  evolve together in this monorepo, so the installed header describes one
  current contract without compatibility branches.

- Added `rpgen_ingest()` as the single composition point for PGEN, PLINK 1
  BED, PED/MAP, TPED/TFAM, VCF/BCF, BGEN, Oxford GEN, HAPS/legend,
  EIGENSTRAT, and legacy PLINK 1 dosage. Destinations include packed
  hardcalls, fixed-point dosage, locus-major haplotypes, and file-backed
  full-precision dosage.

- PGEN and BED now use one persistent pgenlib reader and bounded variant
  panels. The panels enter Rfmalloc through a typed record-buffer context, so
  readers no longer construct a complete genotype matrix or know the
  destination codec layout.

- Added strict phase-preserving reads through `PgrGetP()`. Phased calls are
  written directly to Rfmalloc's locus-major haplotype store. Missing genotypes
  and unphased heterozygotes are rejected instead of being silently invented.

- Reused PLINK 2's import closure for VCF, BCF, BGEN, GEN, HAPS, PED/MAP,
  TPED/TFAM, EIGENSTRAT, and legacy dosage. `rpgen_ingest()` now redirects
  every `STPgenWriter` append shape into the Rfmalloc record sink, eliminating
  temporary PGEN serialization and read-back. File-producing
  `rpgen_import_*()` calls remain unchanged. PED/MAP retains only the bounded
  transpose scratch required to change sample-major input into locus-major
  output. Direct imports report record transfer instead of claiming that a
  nonexistent PGEN was written.

- Added fixture-backed round trips for every supported source, including both
  BGEN layouts, fractional legacy dosage, exact HAPS phase, missing VCF calls,
  phased multiallelic VCF, packed EIGENSTRAT, PLINK 1 PED/TPED layouts, and
  comparison with pgenlibr where available.
