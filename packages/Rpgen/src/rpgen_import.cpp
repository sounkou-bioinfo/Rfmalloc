/*
 * rpgen_import.cpp - milestone 4a: VCF -> .pgen, via plink2's own importer.
 *
 * Hand-authored, not vendored (like rpgen.cpp): it is the glue between
 * Rpgen's existing pgenlib *read* path (rpgen.cpp) and the plink2 *import*
 * closure vendored by tools/vendor-plink2-import/ (plink2_import.cc's
 * VcfToPgen() and everything it pulls in - see that tool's PROVENANCE.md).
 * The design choice this file embodies: reuse plink2's own VcfToPgen()
 * rather than write a separate htslib-based VCF reader, because the same
 * importer closure also covers BCF/BGEN/Oxford for a future milestone - one
 * codebase, not one per format.
 *
 * rpgen_import_vcf() below calls VcfToPgen() with the defaults a plain
 * `plink2 --vcf <path> --make-pgen` (no other flags) would use - see
 * plink-ng's own plink2.cc for the call site and the local default values
 * this mirrors (searched for "VcfToPgen(" there; every default below has a
 * comment citing where plink2.cc got it from). All argv parsing is
 * deliberately skipped: Rpgen has its own R-level argument surface
 * (rpgen_import_vcf() in R/rpgen_import.R), not plink2's command line.
 *
 * plink2's import/write path allocates through a process-global arena
 * ("bigstack": g_bigstack_base/g_bigstack_end, defined in
 * tools/include/plink2_cmdline.cc) that nothing sets up automatically -
 * rpgen_import_vcf() carves it out of the heap via plink2::InitBigstack()
 * before calling VcfToPgen(), and frees it again before returning, goto-
 * cleanup style like rpgen.cpp's rpgen_full_reader_open(). This arena is
 * process-global and not reentrant: only one rpgen_import_vcf() call may be
 * in flight at a time (true by construction for R's single-threaded
 * evaluator, but not safe to call from multiple threads of a larger
 * embedding application).
 *
 * plink2's own logging (logputs()/logerrputs()/logprintf()/...) has been
 * patched, in the vendored tools/include/plink2_cmdline.cc itself (see
 * tools/vendor-plink2-import/patches/plink2_cmdline.cc.patch and its
 * PROVENANCE.md), to route through Rprintf()/REprintf() instead of a log
 * file handle, stdout, or stderr - VcfToPgen() itself never takes an
 * error-message buffer, so a failure's human-readable explanation reaches the user via
 * REprintf() (R's console/stderr), while rpgen_import_vcf()'s own errbuf
 * carries just the numeric plink2::PglErr code for programmatic checks.
 */

#include <cerrno>
#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include <R.h>
#include <Rinternals.h>
#include <R_ext/Rdynload.h>

#include "plink2_cmdline.h"  // InitBigstack(), g_bigstack_base/g_bigstack_end
#include "plink2_common.h"   // InitChrInfoHuman(), CleanupChrInfo()
#include "plink2_import.h"   // VcfToPgen()

// See rpgen.cpp's identical comment: plink2's headers #define FALSE/TRUE as
// int macros, which shadow R's Rboolean enumerators.
#ifdef FALSE
#  undef FALSE
#endif
#ifdef TRUE
#  undef TRUE
#endif

namespace {

// A generous working arena for VcfToPgen(): hundreds of MB, well past what
// a small-to-moderate VCF needs, carved from the heap via plink2's own
// InitBigstack() (tools/include/plink2_cmdline.cc) and freed again once the
// import finishes. See this file's top comment for why the arena exists and
// why it is not reentrant.
constexpr uintptr_t kRpgenImportArenaMib = 512;

// Splits `out_pgen_path` (which must end in ".pgen") into the
// (outname, outname_end) pair VcfToPgen() expects: outname_end is where it
// appends its own suffixes (".pgen", ".pvar"/".pvar.zst", ".psam", each via
// snprintf(outname_end, kMaxOutfnameExtBlen, ...) - see plink2_import.cc).
// outname_buf must be at least kPglFnamesize bytes (the same buffer size
// plink2.cc itself uses for this).
int rpgen_split_pgen_outname(const char *out_pgen_path, char *outname_buf,
                              size_t outname_buf_len, char **outname_end_out,
                              char *errbuf, size_t errbuf_len) {
  static const char kExt[] = ".pgen";
  const size_t ext_len = sizeof(kExt) - 1;
  const size_t len = strlen(out_pgen_path);
  if (len <= ext_len || strcmp(out_pgen_path + (len - ext_len), kExt) != 0) {
    snprintf(errbuf, errbuf_len, "out_pgen_path must end in \".pgen\": \"%s\"",
             out_pgen_path);
    return -1;
  }
  const size_t base_len = len - ext_len;
  // Leave room for the longest suffix VcfToPgen() appends past outname_end.
  if (base_len + plink2::kMaxOutfnameExtBlen >= outname_buf_len) {
    snprintf(errbuf, errbuf_len, "out_pgen_path too long: \"%s\"",
             out_pgen_path);
    return -1;
  }
  memcpy(outname_buf, out_pgen_path, base_len);
  outname_buf[base_len] = '\0';
  *outname_end_out = outname_buf + base_len;
  return 0;
}

// A short, programmatic-friendly gloss on the handful of plink2::PglErr
// codes an import is most likely to fail with. VcfToPgen() itself already
// wrote a detailed, human-readable explanation via logerrputs()/
// logerrprintf() (now routed to REprintf(), see this file's top comment) by
// the time it returns a non-success code, so this string is a supplement,
// not a replacement, for that message.
const char *rpgen_reterr_str(plink2::PglErr reterr) {
  if (reterr == plink2::kPglRetNomem) return "out of memory";
  if (reterr == plink2::kPglRetOpenFail) return "failed to open a file";
  if (reterr == plink2::kPglRetReadFail) return "read failure";
  if (reterr == plink2::kPglRetWriteFail) return "write failure";
  if (reterr == plink2::kPglRetMalformedInput) {
    return "malformed input (is this a valid VCF?)";
  }
  if (reterr == plink2::kPglRetInconsistentInput) return "inconsistent input";
  if (reterr == plink2::kPglRetDecompressFail) return "decompression failure";
  return "see the R console/stderr for plink2's own diagnostic message";
}

// Converts vcf_path to out_pgen_path (which must end in ".pgen") by calling
// plink2's own VcfToPgen() importer with the defaults a plain
// `--vcf <path> --make-pgen` implies (see this file's top comment). Single-
// exit cleanup via goto, same shape as rpgen.cpp's rpgen_full_reader_open():
// every resource acquired below (the bigstack arena, ChrInfo's own
// allocations) is released exactly once at `cleanup`, regardless of which
// step failed.
int rpgen_import_vcf(const char *vcf_path, const char *out_pgen_path,
                      char *errbuf, size_t errbuf_len) {
  if (errbuf_len > 0) {
    errbuf[0] = '\0';
  }

  int rc = -1;
  unsigned char *bigstack_ua = nullptr;
  bool chr_info_ready = false;
  plink2::ChrInfo chr_info;
  char outname[plink2::kPglFnamesize];
  char *outname_end = nullptr;

  if (rpgen_split_pgen_outname(out_pgen_path, outname, sizeof(outname),
                                &outname_end, errbuf, errbuf_len) != 0) {
    return -1;
  }

  {
    uintptr_t malloc_mib_final = 0;
    if (plink2::InitBigstack(kRpgenImportArenaMib, &malloc_mib_final,
                              &bigstack_ua) != plink2::kPglRetSuccess) {
      snprintf(errbuf, errbuf_len,
               "failed to allocate a %lu MiB working arena",
               static_cast<unsigned long>(kRpgenImportArenaMib));
      goto cleanup;
    }
  }

  if (plink2::InitChrInfoHuman(&chr_info) != plink2::kPglRetSuccess) {
    snprintf(errbuf, errbuf_len, "failed to initialize chromosome info");
    goto cleanup;
  }
  chr_info_ready = true;

  {
    uint32_t pgen_generated = 0;
    uint32_t psam_generated = 0;
    const plink2::PglErr reterr = plink2::VcfToPgen(
        vcf_path,
        /*preexisting_psamname=*/nullptr,  // no separate .psam; sample IDs come from the VCF header
        /*const_fid=*/nullptr,             // plink2.cc default: no constant family ID
        /*dosage_import_field=*/nullptr,   // plink2.cc default: no --vcf-dosage field requested
        /*missing_varid=*/".",             // plink2.cc default missing_varid ('.' via g_one_char_strs there)
        /*misc_flags=*/plink2::kfMisc0,               // plink2.cc default (no --allow-extra-chr etc.)
        /*import_flags=*/plink2::kfImportKeepAutoconv,  // uncompressed .pvar/.psam; irrelevant to the .pgen bytes
        /*load_filter_log_import_flags=*/plink2::kfLoadFilterLog0,
        /*no_samples_ok=*/0,          // we always want genotypes, so require samples
        /*is_update_or_impute_sex=*/0,  // no --update-sex/--impute-sex
        /*is_splitpar=*/0,            // no --split-par
        /*is_sortvars=*/0,            // plink2.cc default: pc.sort_vars_mode starts at kSort0 (<= kSortNone)
        /*hard_call_thresh=*/UINT32_MAX,  // plink2.cc default: VcfToPgen() substitutes kDosageMid / 10 itself
        /*dosage_erase_thresh=*/0,        // plink2.cc default
        /*import_dosage_certainty=*/0.0,  // plink2.cc default
        /*id_delim=*/'\0',                // plink2.cc default: no --id-delim
        /*idspace_to=*/'\0',              // plink2.cc default
        /*vcf_min_gq=*/-1,                // plink2.cc default: no --vcf-min-gq
        /*vcf_min_dp=*/-1,                // plink2.cc default: no --vcf-min-dp
        /*vcf_max_dp=*/0x7fffffff,        // plink2.cc default: no --vcf-max-dp
        /*halfcall_mode=*/plink2::kVcfHalfCallDefault,  // plink2.cc default
        /*fam_cols=*/plink2::kfFamCol13456,             // plink2.cc default (all .psam/.fam columns)
        /*import_max_allele_ct=*/0x7ffffffe,            // plink2.cc default (effectively unlimited)
        /*overlong_varids_mode=*/plink2::kImportOverlongVarIds0,  // plink2.cc default
        /*max_thread_ct=*/1,  // conservative single-threaded default for a library call
        outname, outname_end, &chr_info, &pgen_generated, &psam_generated);
    if (reterr != plink2::kPglRetSuccess) {
      snprintf(errbuf, errbuf_len, "VcfToPgen(\"%s\") failed: %s (code %d)",
               vcf_path, rpgen_reterr_str(reterr), static_cast<int>(reterr));
      goto cleanup;
    }
    if (!pgen_generated) {
      snprintf(errbuf, errbuf_len,
               "VcfToPgen(\"%s\") reported no .pgen written (no samples?)",
               vcf_path);
      goto cleanup;
    }
    (void)psam_generated;
  }

  rc = 0;

cleanup:
  if (chr_info_ready) {
    plink2::CleanupChrInfo(&chr_info);
  }
  if (bigstack_ua) {
    free(bigstack_ua);
  }
  // Never leave dangling pointers into freed memory in these process
  // globals, whether or not InitBigstack() itself ever set them.
  plink2::g_bigstack_base = nullptr;
  plink2::g_bigstack_end = nullptr;
  return rc;
}

}  // namespace

extern "C" {

// See inst/include/Rpgen.h's Rpgen_import_vcf_fun doc comment for the full
// contract; this just forwards to the implementation above.
int Rpgen_import_vcf(const char *vcf_path, const char *out_pgen_path,
                      char *errbuf, size_t errbuf_len) {
  return rpgen_import_vcf(vcf_path, out_pgen_path, errbuf, errbuf_len);
}

SEXP RC_rpgen_import_vcf(SEXP vcf_sexp, SEXP out_sexp) {
  if (TYPEOF(vcf_sexp) != STRSXP || Rf_length(vcf_sexp) != 1 ||
      STRING_ELT(vcf_sexp, 0) == NA_STRING) {
    Rf_error("vcf must be a single non-NA string");
  }
  if (TYPEOF(out_sexp) != STRSXP || Rf_length(out_sexp) != 1 ||
      STRING_ELT(out_sexp, 0) == NA_STRING) {
    Rf_error("out must be a single non-NA string");
  }
  const char *vcf_path = CHAR(STRING_ELT(vcf_sexp, 0));
  const char *out_pgen_path = CHAR(STRING_ELT(out_sexp, 0));

  char errbuf[512];
  if (Rpgen_import_vcf(vcf_path, out_pgen_path, errbuf, sizeof(errbuf)) != 0) {
    Rf_error("rpgen_import_vcf(\"%s\") failed: %s", vcf_path, errbuf);
  }
  return Rf_mkString(out_pgen_path);
}

}  // extern "C"
