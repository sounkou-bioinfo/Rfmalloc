#ifndef RPGEN_API_PUBLIC_H
#define RPGEN_API_PUBLIC_H

/*
 * Rpgen.h - C-callable API for the Rpgen carrier package, version 4.
 *
 * Rpgen vendors the read subset of PLINK 2's pgenlib
 * (https://github.com/chrchang/plink-ng) - the same subset the CRAN package
 * 'pgenlibr' vendors and builds as libPLINK2.a - and exposes it through
 * R_RegisterCCallable() entry points. Unlike pgenlibr, which only offers an
 * R-level (Rcpp) interface, Rpgen's whole reason to exist is letting other R
 * packages link to a compiled pgenlib and read .pgen genotypes natively from
 * their own C or C++ code, without re-vendoring pgenlib themselves.
 *
 * Usage:
 *   1. List Rpgen under LinkingTo (and Imports, so it is loaded first) in
 *      your package's DESCRIPTION.
 *   2. #include <Rpgen.h> in your C/C++ source.
 *   3. Call R_GetCCallable("Rpgen", "Rpgen_open_info") (or use the
 *      Rpgen_open_info_fun typedef below to keep the call site typed) to
 *      resolve the symbol, then invoke it. R_GetCCallable() aborts the R
 *      session if Rpgen has not been loaded yet, so only call it once
 *      Rpgen is guaranteed attached/loaded (normal LinkingTo + Imports
 *      behavior guarantees this).
 *
 * Milestone 1 provides Rpgen_open_info(): open a .pgen file far enough to
 * read its header counts, then close it again.
 *
 * Milestone 2 adds Rpgen_read_hardcalls()/Rpgen_read_dosages(): read a
 * variant range for every sample into a caller-allocated buffer. Neither
 * milestone keeps a reader handle alive across calls - every call opens the
 * file, does its work, and closes it again.
 *
 * Milestone 3 adds Rpgen_read_bed_hardcalls(): read every variant, for every
 * sample, from a PLINK 1 .bed file - the same underlying PgfiInitPhase1() /
 * PgrGet() path as Rpgen_read_hardcalls(), except a .bed has no header to
 * read raw_sample_ct/raw_variant_ct back from, so the caller must supply
 * both (typically the line counts of the companion .fam/.bim).
 *
 * Milestone 4a adds Rpgen_import_vcf(): convert a VCF straight to a .pgen by
 * calling plink2's own VcfToPgen() importer (a separate vendored closure,
 * tools/vendor-plink2-import/ - see its PROVENANCE.md), then read the result
 * with any of the functions above. Unlike them, this one is not a pure
 * reader: it allocates plink2's own working arena for the duration of the
 * call (see src/rpgen_import.cpp) and is not reentrant - only one call may
 * be in flight at a time in this process.
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

#ifdef __cplusplus
}
#endif

#endif /* RPGEN_API_PUBLIC_H */
