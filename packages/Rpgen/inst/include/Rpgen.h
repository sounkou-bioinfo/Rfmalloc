#ifndef RPGEN_API_PUBLIC_H
#define RPGEN_API_PUBLIC_H

/*
 * Rpgen.h - low-level C-callables over PLINK 2's pgenlib and importers.
 *
 * Add Rpgen to LinkingTo and Imports, include this header, and resolve the
 * typed entry point with R_GetCCallable(). The range readers below are
 * stateless helpers: each opens a source, fills a caller-owned buffer, and
 * closes it. Rpgen's Rfmalloc ingestion path instead keeps one reader open
 * across bounded panels and is intentionally package-internal for now.
 *
 * The import functions call PLINK 2's own VCF, BCF, BGEN, GEN, HAPS,
 * PLINK 1 dosage, PED/MAP, TPED/TFAM, and EIGENSTRAT closure. They use
 * PLINK 2's process-global bigstack and are not reentrant. HAPS output
 * retains phase; the R-level rpgen_haplotypes() path reads it through
 * PgrGetP(). The hardcall and dosage callables here expose the
 * phase-collapsed non-reference count.
 */

#include <stddef.h>
#include <stdint.h>

#include <R_ext/Rdynload.h>

#ifdef __cplusplus
extern "C" {
#endif

/* -- version / identity ---------------------------------------------------- */

typedef int (*Rpgen_api_version_fun)(void);

/* -- open a .pgen far enough to read its header counts ---------------------- */

/*
 * Rpgen_open_info(path, n_sample_out, n_variant_out, errbuf, errbuf_len)
 *
 * Opens the .pgen file at `path` (PgfiInitPhase1 / PgfiInitPhase2 / PgrInit),
 * reads plink2::PgenFileInfo's raw_sample_ct / raw_variant_ct, and closes
 * everything again. Returns 0 on success, with *n_sample_out/*n_variant_out
 * filled in; returns nonzero on failure, with a NUL-terminated message
 * written into errbuf (a caller-owned buffer of at least errbuf_len bytes).
 */
typedef int (*Rpgen_open_info_fun)(const char *path, uint32_t *n_sample_out,
                                   uint32_t *n_variant_out, char *errbuf,
                                   size_t errbuf_len);

/* -- read genotypes ---------------------------------------------------------- */

/*
 * Rpgen_read_hardcalls(path, variant_start, variant_ct, out, n_sample_out,
 *                       n_variant_out, errbuf, errbuf_len)
 *
 * Opens the .pgen file at `path` and reads variants
 * [variant_start, variant_start + variant_ct) for every sample via
 * plink2::PgrGet(), then closes it again. `out` must be a caller-allocated
 * buffer of at least (the file's raw_sample_ct) * variant_ct int32_t values,
 * written in column-major (variant-major) order:
 * out[v * raw_sample_ct + s] holds sample s's hardcall dosage - 0, 1, 2, or
 * NA_INTEGER for missing - at variant (variant_start + v). This is exactly
 * the layout an R INTSXP matrix of dim (raw_sample_ct, variant_ct) uses, so
 * INTEGER(result) can be passed straight through from a .Call entry point.
 *
 * For a multiallelic variant, every ALT allele is collapsed into one
 * non-reference count (plink2::PgrGet(), unlike the allele-specific
 * PgrGet1(), needs no per-variant allele-identity bookkeeping to produce
 * this - see Rpgen's own rpgen_read_hardcalls()/rpgen_read_dosages() R
 * documentation for why that means no .pvar is required here, unlike
 * pgenlibr::NewPgen() at a file combining multiallelic variants with
 * phase/dosage info).
 *
 * *n_sample_out is always the file's full raw_sample_ct (there is no
 * sample-subsetting API yet); *n_variant_out is the file's full
 * raw_variant_ct, independent of variant_ct, so callers can distinguish a
 * short read (variant_ct < raw_variant_ct, by design) from an out-of-range
 * one (checked internally: variant_start + variant_ct > raw_variant_ct
 * fails). Returns 0 on success, nonzero on failure with a NUL-terminated
 * message in errbuf (a caller-owned buffer of at least errbuf_len bytes).
 */
typedef int (*Rpgen_read_hardcalls_fun)(const char *path,
                                        uint32_t variant_start,
                                        uint32_t variant_ct, int32_t *out,
                                        uint32_t *n_sample_out,
                                        uint32_t *n_variant_out, char *errbuf,
                                        size_t errbuf_len);

/*
 * Rpgen_read_dosages(path, variant_start, variant_ct, out, n_sample_out,
 *                     n_variant_out, errbuf, errbuf_len)
 *
 * Same contract as Rpgen_read_hardcalls(), but reads via plink2::PgrGetD()
 * and `out` is a caller-allocated double buffer: out[v * raw_sample_ct + s]
 * holds sample s's dosage in [0, 2] (or NA_REAL for missing) at variant
 * (variant_start + v). A sample with no explicit dosage record at a variant
 * falls back to that sample's hardcall value, so every non-missing sample
 * gets a value.
 */
typedef int (*Rpgen_read_dosages_fun)(const char *path, uint32_t variant_start,
                                      uint32_t variant_ct, double *out,
                                      uint32_t *n_sample_out,
                                      uint32_t *n_variant_out, char *errbuf,
                                      size_t errbuf_len);

/* -- read genotypes from a PLINK 1 .bed --------------------------------- */

/*
 * Rpgen_read_bed_hardcalls(bed_path, raw_sample_ct, raw_variant_ct, out,
 *                           errbuf, errbuf_len)
 *
 * Opens the PLINK 1 .bed file at `bed_path` and reads every variant, for
 * every sample, via plink2::PgrGet() (the same reader Rpgen_read_hardcalls()
 * uses - PgfiInitPhase1() opens a .bed transparently, in the same code path
 * as a .pgen), then closes it again. Unlike Rpgen_read_hardcalls(), a .bed
 * has no header to read counts back from: the caller must already know
 * `raw_sample_ct`/`raw_variant_ct` (typically the line counts of the
 * companion .fam/.bim) and supplies them; pgenlib validates them against the
 * .bed's file size and fails if they're wrong.
 *
 * `out` must be a caller-allocated buffer of at least
 * raw_sample_ct * raw_variant_ct int32_t values, written in column-major
 * (variant-major) order: out[v * raw_sample_ct + s] holds sample s's
 * hardcall dosage - 0, 1, 2, or NA_INTEGER for missing - at variant v. This
 * is exactly the layout an R INTSXP matrix of dim (raw_sample_ct,
 * raw_variant_ct) uses, so INTEGER(result) can be passed straight through
 * from a .Call entry point.
 *
 * A PLINK 1 .bed is biallelic hardcalls only - there is no dosage
 * counterpart to this function. Returns 0 on success, nonzero on failure
 * with a NUL-terminated message in errbuf (a caller-owned buffer of at
 * least errbuf_len bytes).
 */
typedef int (*Rpgen_read_bed_hardcalls_fun)(const char *bed_path,
                                            uint32_t raw_sample_ct,
                                            uint32_t raw_variant_ct,
                                            int32_t *out, char *errbuf,
                                            size_t errbuf_len);

/* -- import a VCF to a .pgen ------------------------------------------------ */

/*
 * Rpgen_import_vcf(vcf_path, out_pgen_path, errbuf, errbuf_len)
 *
 * Converts the VCF at `vcf_path` to a .pgen at `out_pgen_path` (which must
 * end in ".pgen") by calling plink2's own VcfToPgen() importer, with the
 * defaults a plain `plink2 --vcf <vcf_path> --make-pgen` (no other flags)
 * would use. The companion .pvar/.psam are written next to it, same base
 * name, conventional extensions. Returns 0 on success, nonzero on failure
 * with a NUL-terminated message in errbuf (a caller-owned buffer of at
 * least errbuf_len bytes) - VcfToPgen() itself has no error-message-buffer
 * parameter, so a failure's human-readable explanation was already relayed
 * to the R console/stderr by the time this returns (see src/rpgen_import.cpp's
 * top comment); errbuf carries only a short, programmatic-friendly gloss
 * plus the numeric plink2::PglErr code.
 *
 * Allocates and frees plink2's own "bigstack" working arena for the
 * duration of this one call (process-global, not reentrant - see
 * src/rpgen_import.cpp's top comment). Read the resulting .pgen with
 * Rpgen_open_info()/Rpgen_read_hardcalls()/Rpgen_read_dosages() above, same
 * as any other .pgen.
 */
typedef int (*Rpgen_import_vcf_fun)(const char *vcf_path,
                                    const char *out_pgen_path, char *errbuf,
                                    size_t errbuf_len);

/* -- import other PLINK 2-supported sources to a .pgen -------------------- */

/*
 * Rpgen_import_bcf(bcf_path, out_pgen_path, errbuf, errbuf_len)
 *
 * Same contract as Rpgen_import_vcf() above, but for BCF (VCF's binary
 * sibling) via plink2's own BcfToPgen() - the defaults a plain
 * `plink2 --bcf <bcf_path> --make-pgen` would use.
 */
typedef int (*Rpgen_import_bcf_fun)(const char *bcf_path,
                                    const char *out_pgen_path, char *errbuf,
                                    size_t errbuf_len);

/*
 * Rpgen_import_gen(gen_path, sample_path, out_pgen_path, errbuf, errbuf_len)
 *
 * Converts the Oxford-format `gen_path`/`sample_path` pair to a .pgen at
 * `out_pgen_path` by calling plink2's own OxGenToPgen() importer, with the
 * defaults a plain `plink2 --gen <gen_path> --sample <sample_path>
 * --make-pgen` would use (this library entry point does not require the
 * newer plink2 CLI's mandatory ref-first/ref-last/ref-unknown modifier - see
 * src/rpgen_import.cpp's top comment for why the library-level default is
 * safe and unambiguous). `sample_path` must be non-null: a .gen file carries
 * no sample IDs of its own. `gen_path`'s rows must carry an explicit leading
 * chromosome column (plink2's original 5-column .gen layout). Same
 * arena/reentrancy caveat as Rpgen_import_vcf().
 */
typedef int (*Rpgen_import_gen_fun)(const char *gen_path,
                                    const char *sample_path,
                                    const char *out_pgen_path, char *errbuf,
                                    size_t errbuf_len);

/*
 * Rpgen_import_bgen(bgen_path, sample_path, out_pgen_path, errbuf,
 *                    errbuf_len)
 *
 * Same as Rpgen_import_gen(), but for a BGEN file (any of v1.1/1.2/1.3) via
 * plink2's own OxBgenToPgen() importer. Unlike Rpgen_import_gen(),
 * `sample_path` may be NULL: BGEN v1.2/v1.3 files may carry their own sample
 * identifier block, in which case no external .sample file is needed.
 */
typedef int (*Rpgen_import_bgen_fun)(const char *bgen_path,
                                     const char *sample_path,
                                     const char *out_pgen_path, char *errbuf,
                                     size_t errbuf_len);

/*
 * Rpgen_import_haps(haps_path, legend_path, sample_path, chr, out_pgen_path,
 *                    errbuf, errbuf_len)
 *
 * Converts the Oxford-format `haps_path`/`legend_path`/`sample_path` triple
 * (all required) to a .pgen at `out_pgen_path` by calling plink2's own
 * OxHapslegendToPgen() importer, with the defaults a plain
 * `plink2 --haps <haps_path> --legend <legend_path> <chr> --sample
 * <sample_path> --make-pgen` would use. A .haps/.legend pair encodes
 * *phased* haplotypes; the produced .pgen carries that phase, but none of
 * this header's reader functions (Rpgen_read_hardcalls()/
 * Rpgen_read_dosages()) read phase back yet - only the collapsed
 * hardcall/dosage view is available today. Same arena/reentrancy caveat as
 * Rpgen_import_vcf().
 *
 * `chr` (a chromosome code, e.g. "1") is required and must be non-null: the
 * classic IMPUTE2 .legend format has no chromosome column of its own, so
 * plink2 itself requires one via `--legend <filename> <chr code>` whenever
 * --legend is used - passing a null `chr` here reaches an unchecked null
 * dereference inside the vendored OxHapslegendToPgen()'s
 * InitOxfordSingleChr() call, crashing the whole process instead of
 * returning an error (see src/rpgen_import.cpp's rpgen_import_haps()
 * comment for how this was found).
 */
typedef int (*Rpgen_import_haps_fun)(const char *haps_path,
                                     const char *legend_path,
                                     const char *sample_path, const char *chr,
                                     const char *out_pgen_path, char *errbuf,
                                     size_t errbuf_len);

/*
 * Rpgen_import_plink1_dosage(dosage_path, fam_path, map_path, out_pgen_path,
 *                             errbuf, errbuf_len)
 *
 * Converts the legacy PLINK 1.x `--import-dosage` text format
 * (`dosage_path`, plus companion `fam_path`/`map_path`, all required) to a
 * .pgen at `out_pgen_path` by calling plink2's own Plink1DosageToPgen()
 * importer, with the defaults a plain
 * `plink2 --import-dosage <dosage_path> --fam <fam_path> --map <map_path>
 * --make-pgen` (no --import-dosage modifiers) would use - in particular, the
 * per-sample column format (single dosage value vs. a double/triple
 * probability layout) is auto-inferred from the file's own column count, the
 * same as the plain command would do. Same arena/reentrancy caveat as
 * Rpgen_import_vcf().
 */
typedef int (*Rpgen_import_plink1_dosage_fun)(const char *dosage_path,
                                               const char *fam_path,
                                               const char *map_path,
                                               const char *out_pgen_path,
                                               char *errbuf,
                                               size_t errbuf_len);

/*
 * Rpgen_import_ped(ped_path, map_path, out_pgen_path, errbuf, errbuf_len)
 *
 * Converts a PLINK 1 sample-major .ped/.map pair with plink2's own
 * PedmapToPgen() importer and plain-command defaults. Both source paths are
 * required. Same output and arena/reentrancy contract as
 * Rpgen_import_vcf().
 */
typedef int (*Rpgen_import_ped_fun)(const char *ped_path,
                                    const char *map_path,
                                    const char *out_pgen_path, char *errbuf,
                                    size_t errbuf_len);

/*
 * Rpgen_import_tped(tped_path, tfam_path, out_pgen_path, errbuf, errbuf_len)
 *
 * Converts a PLINK 1 variant-major .tped/.tfam pair with plink2's own
 * TpedToPgen() importer and plain-command defaults. Both source paths are
 * required. Same output and arena/reentrancy contract as
 * Rpgen_import_vcf().
 */
typedef int (*Rpgen_import_tped_fun)(const char *tped_path,
                                     const char *tfam_path,
                                     const char *out_pgen_path, char *errbuf,
                                     size_t errbuf_len);

/*
 * Rpgen_import_eigenstrat(geno_path, ind_path, snp_path, out_pgen_path,
 *                          errbuf, errbuf_len)
 *
 * Converts EIGENSOFT PACKEDANCESTRYMAP or TGENO data with its .ind/.snp
 * companions via plink2's EigfileToPgen() importer. The binary header's
 * sample and variant hashes are validated. All three source paths are
 * required. Same output and arena/reentrancy contract as
 * Rpgen_import_vcf().
 */
typedef int (*Rpgen_import_eigenstrat_fun)(const char *geno_path,
                                           const char *ind_path,
                                           const char *snp_path,
                                           const char *out_pgen_path,
                                           char *errbuf,
                                           size_t errbuf_len);

#ifdef __cplusplus
}
#endif

#endif /* RPGEN_API_PUBLIC_H */
