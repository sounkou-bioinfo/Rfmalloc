/*
 * rpgen_import.cpp - milestone 4a/4b: the whole plink2 import matrix, via
 * plink2's own importers.
 *
 * Hand-authored, not vendored (like rpgen.cpp): it is the glue between
 * Rpgen's existing pgenlib *read* path (rpgen.cpp) and the plink2 *import*
 * closure vendored by tools/vendor-plink2-import/ (plink2_import.cc's
 * VcfToPgen() and everything it pulls in - see that tool's PROVENANCE.md).
 * The design choice this file embodies: reuse plink2's own import functions
 * rather than write a separate from-scratch reader per format, because they
 * all live in the one vendored closure already - one codebase, not one per
 * format.
 *
 * Milestone 4a added rpgen_import_vcf() (VcfToPgen()). Milestone 4b closes
 * over the rest of the same closure's public entry points: rpgen_import_bcf()
 * (BcfToPgen()), rpgen_import_bgen() (OxBgenToPgen()), rpgen_import_gen()
 * (OxGenToPgen()), rpgen_import_haps() (OxHapslegendToPgen()), and
 * rpgen_import_plink1_dosage() (Plink1DosageToPgen()). No new vendoring was
 * needed: every one of these already lives in plink2_import.cc, which
 * tools/vendor-plink2-import/ pulled in wholesale for VcfToPgen()'s sake back
 * in 4a.
 *
 * Every driver below calls its plink2 import function with the defaults a
 * plain `plink2 <format flag> <path(s)> --make-pgen` (no other flags) would
 * use - see plink-ng's own plink2.cc for the call site and the local default
 * values each mirrors (searched for e.g. "BcfToPgen(" there; every default
 * has a comment citing where plink2.cc got it from). All argv parsing is
 * deliberately skipped: Rpgen has its own R-level argument surface
 * (R/rpgen_import.R), not plink2's command line. One deliberate departure
 * from a literal plain-command default, shared by every Oxford-format driver
 * in this file: plink2's own CLI now rejects a bare `--gen`/`--bgen`/`--data`
 * without an explicit ref-first/ref-last/ref-unknown modifier (a
 * command-line safety net added after the underlying library functions were
 * written), but the vendored OxGenToPgen()/OxBgenToPgen()/
 * OxHapslegendToPgen() functions themselves still accept zero
 * OxfordImportFlags bits and fall back to their own documented behavior in
 * that case (treat the second allele column as the provisional reference -
 * see each function's own comment in plink2_import.cc); every driver below
 * passes kfOxfordImport0 for exactly that library-level default, cross-
 * checked against a real plink2 build (see
 * inst/tinytest/test_import_matrix.R's header comment for how).
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
 * The vendored closure's rare exit()/abort() calls (a handful of "should
 * never happen" internal-error paths in plink2_data.cc/plink2_import.cc)
 * are redirected by rpgen_cli_shim.h to rpgen_plink2_exit()/
 * rpgen_plink2_abort() (rpgen_plink2_glue.h/.c), which longjmp back into
 * this function instead of calling Rf_error() directly - see
 * rpgen_plink2_glue.h's top comment for why (in short: Rf_error() would
 * longjmp past the arena free below). rpgen_import_vcf() setjmp()s that
 * same jmp_buf before making any call into the vendored closure, so a
 * longjmp lands back here and falls through this function's normal
 * goto-cleanup path, same as an ordinary PglErr failure.
 *
 * plink2's own logging (logputs()/logerrputs()/logprintf()/...) has been
 * patched, in the vendored tools/include/plink2_cmdline.cc itself (see
 * tools/vendor-plink2-import/patches/plink2_cmdline.cc.patch and its
 * PROVENANCE.md), to route through Rprintf()/REprintf() instead of a log
 * file handle, stdout, or stderr - VcfToPgen() itself never takes an
 * error-message buffer, so a failure's human-readable explanation reaches
 * the user via REprintf() (R's console/stderr), while rpgen_import_vcf()'s
 * own errbuf carries just the numeric plink2::PglErr code for programmatic
 * checks.
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

#include <csetjmp>

#include "rpgen_plink2_glue.h"  // rpgen_plink2_exit_jmp/_exit_code/_aborted

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
  // volatile: both are (re)assigned after the setjmp() below and read again
  // in the `cleanup` path a longjmp may jump straight to, which the C/C++
  // standard only guarantees survives correctly for volatile-qualified
  // locals (rpgen_plink2_glue.h explains why a longjmp can land here at
  // all). chr_info itself does not need the same treatment: its address is
  // taken and handed to external, separately-compiled functions
  // (InitChrInfoHuman()/VcfToPgen()/CleanupChrInfo()), which forces the
  // compiler to keep it memory-resident rather than register-cached.
  unsigned char *volatile bigstack_ua = nullptr;
  volatile bool chr_info_ready = false;
  plink2::ChrInfo chr_info;
  char outname[plink2::kPglFnamesize];
  char *outname_end = nullptr;

  if (rpgen_split_pgen_outname(out_pgen_path, outname, sizeof(outname),
                                &outname_end, errbuf, errbuf_len) != 0) {
    return -1;
  }

  // Guards every call from here on into the vendored plink2 closure: see
  // this file's top comment and rpgen_plink2_glue.h for why. A nonzero
  // return means some vendored code called exit()/abort() instead of
  // returning a PglErr the normal way; report it like any other failure
  // path in this function and fall through to the same `cleanup`.
  if (setjmp(rpgen_plink2_exit_jmp) != 0) {
    if (rpgen_plink2_aborted) {
      snprintf(errbuf, errbuf_len,
               "VcfToPgen(\"%s\") aborted (internal plink2 abort())",
               vcf_path);
    } else {
      snprintf(errbuf, errbuf_len,
               "VcfToPgen(\"%s\") called exit(%d) internally", vcf_path,
               rpgen_plink2_exit_code);
    }
    goto cleanup;
  }

  {
    uintptr_t malloc_mib_final = 0;
    // Landed through InitBigstack()'s own unsigned char** out-param, not
    // straight into the volatile bigstack_ua above: a volatile-qualified
    // pointer variable's address does not implicitly convert to the plain
    // unsigned char** InitBigstack() expects. Assigning the plain result
    // into bigstack_ua right after success (below) still records it before
    // any later vendored call that might exit()/abort().
    unsigned char *bigstack_ua_tmp = nullptr;
    if (plink2::InitBigstack(kRpgenImportArenaMib, &malloc_mib_final,
                              &bigstack_ua_tmp) != plink2::kPglRetSuccess) {
      snprintf(errbuf, errbuf_len,
               "failed to allocate a %lu MiB working arena",
               static_cast<unsigned long>(kRpgenImportArenaMib));
      goto cleanup;
    }
    bigstack_ua = bigstack_ua_tmp;
  }

  if (plink2::InitChrInfoHuman(&chr_info) != plink2::kPglRetSuccess) {
    snprintf(errbuf, errbuf_len, "failed to initialize chromosome info");
    goto cleanup;
  }
  chr_info_ready = true;

  {
    // VcfToPgen() only ever *clears* these to 0 (when the VCF has no
    // samples); it never sets them to 1 itself. Callers must pre-set them
    // to 1, exactly as plink2.cc's own call site does - initializing to 0
    // here would make the "was a .pgen actually written" check below fire
    // on every successful import.
    uint32_t pgen_generated = 1;
    uint32_t psam_generated = 1;
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

// Converts bcf_path to out_pgen_path via plink2's own BcfToPgen() - the same
// function signature as VcfToPgen() above (BCF is VCF's binary sibling; the
// vendored closure shares one code path for both, branching internally on
// the container format), so every default here is identical to
// rpgen_import_vcf()'s for the same plink2.cc-cited reason. See that
// function's comment for the arena/jmp_buf/goto-cleanup mechanics, which are
// identical here too.
int rpgen_import_bcf(const char *bcf_path, const char *out_pgen_path,
                      char *errbuf, size_t errbuf_len) {
  if (errbuf_len > 0) {
    errbuf[0] = '\0';
  }

  int rc = -1;
  unsigned char *volatile bigstack_ua = nullptr;
  volatile bool chr_info_ready = false;
  plink2::ChrInfo chr_info;
  char outname[plink2::kPglFnamesize];
  char *outname_end = nullptr;

  if (rpgen_split_pgen_outname(out_pgen_path, outname, sizeof(outname),
                                &outname_end, errbuf, errbuf_len) != 0) {
    return -1;
  }

  if (setjmp(rpgen_plink2_exit_jmp) != 0) {
    if (rpgen_plink2_aborted) {
      snprintf(errbuf, errbuf_len,
               "BcfToPgen(\"%s\") aborted (internal plink2 abort())",
               bcf_path);
    } else {
      snprintf(errbuf, errbuf_len,
               "BcfToPgen(\"%s\") called exit(%d) internally", bcf_path,
               rpgen_plink2_exit_code);
    }
    goto cleanup;
  }

  {
    uintptr_t malloc_mib_final = 0;
    unsigned char *bigstack_ua_tmp = nullptr;
    if (plink2::InitBigstack(kRpgenImportArenaMib, &malloc_mib_final,
                              &bigstack_ua_tmp) != plink2::kPglRetSuccess) {
      snprintf(errbuf, errbuf_len,
               "failed to allocate a %lu MiB working arena",
               static_cast<unsigned long>(kRpgenImportArenaMib));
      goto cleanup;
    }
    bigstack_ua = bigstack_ua_tmp;
  }

  if (plink2::InitChrInfoHuman(&chr_info) != plink2::kPglRetSuccess) {
    snprintf(errbuf, errbuf_len, "failed to initialize chromosome info");
    goto cleanup;
  }
  chr_info_ready = true;

  {
    uint32_t pgen_generated = 1;
    uint32_t psam_generated = 1;
    const plink2::PglErr reterr = plink2::BcfToPgen(
        bcf_path,
        /*preexisting_psamname=*/nullptr,  // no separate .psam; sample IDs come from the BCF header
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
        /*hard_call_thresh=*/UINT32_MAX,  // plink2.cc default: BcfToPgen() substitutes kDosageMid / 10 itself
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
      snprintf(errbuf, errbuf_len, "BcfToPgen(\"%s\") failed: %s (code %d)",
               bcf_path, rpgen_reterr_str(reterr), static_cast<int>(reterr));
      goto cleanup;
    }
    if (!pgen_generated) {
      snprintf(errbuf, errbuf_len,
               "BcfToPgen(\"%s\") reported no .pgen written (no samples?)",
               bcf_path);
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
  plink2::g_bigstack_base = nullptr;
  plink2::g_bigstack_end = nullptr;
  return rc;
}

// Converts genname/samplename to out_pgen_path via plink2's own
// OxGenToPgen() (classic Oxford .gen + .sample). Unlike VCF/BCF, a .gen file
// carries no sample IDs or pedigree information of its own - samplename is
// therefore a required argument here (see R/rpgen_import.R's
// rpgen_import_gen(), which has no default for `sample`), not merely a
// preexisting-psam override. genname's rows are also expected to carry an
// explicit leading chromosome column (plink2's "original 5-column .gen"
// layout, readable without an --oxford-single-chr override), which is what
// this driver's own tinytest fixture writes.
int rpgen_import_gen(const char *gen_path, const char *sample_path,
                      const char *out_pgen_path, char *errbuf,
                      size_t errbuf_len) {
  if (errbuf_len > 0) {
    errbuf[0] = '\0';
  }

  int rc = -1;
  unsigned char *volatile bigstack_ua = nullptr;
  volatile bool chr_info_ready = false;
  plink2::ChrInfo chr_info;
  char outname[plink2::kPglFnamesize];
  char *outname_end = nullptr;

  if (rpgen_split_pgen_outname(out_pgen_path, outname, sizeof(outname),
                                &outname_end, errbuf, errbuf_len) != 0) {
    return -1;
  }

  if (setjmp(rpgen_plink2_exit_jmp) != 0) {
    if (rpgen_plink2_aborted) {
      snprintf(errbuf, errbuf_len,
               "OxGenToPgen(\"%s\") aborted (internal plink2 abort())",
               gen_path);
    } else {
      snprintf(errbuf, errbuf_len,
               "OxGenToPgen(\"%s\") called exit(%d) internally", gen_path,
               rpgen_plink2_exit_code);
    }
    goto cleanup;
  }

  {
    uintptr_t malloc_mib_final = 0;
    unsigned char *bigstack_ua_tmp = nullptr;
    if (plink2::InitBigstack(kRpgenImportArenaMib, &malloc_mib_final,
                              &bigstack_ua_tmp) != plink2::kPglRetSuccess) {
      snprintf(errbuf, errbuf_len,
               "failed to allocate a %lu MiB working arena",
               static_cast<unsigned long>(kRpgenImportArenaMib));
      goto cleanup;
    }
    bigstack_ua = bigstack_ua_tmp;
  }

  if (plink2::InitChrInfoHuman(&chr_info) != plink2::kPglRetSuccess) {
    snprintf(errbuf, errbuf_len, "failed to initialize chromosome info");
    goto cleanup;
  }
  chr_info_ready = true;

  {
    const plink2::PglErr reterr = plink2::OxGenToPgen(
        gen_path, sample_path,
        /*const_fid=*/nullptr,       // plink2.cc default: no constant family ID
        /*ox_single_chr_str=*/nullptr,  // plink2.cc default: no --oxford-single-chr (genname supplies its own chr column)
        /*ox_missing_code=*/nullptr,    // plink2.cc default: no --missing-code (OxSampleToPsam() falls back to "NA")
        /*missing_catname=*/"NONE",     // plink2.cc default (pc.missing_catname is unconditionally set to "NONE" before flag parsing)
        /*missing_varid=*/".",          // plink2.cc default missing_varid, same as VcfToPgen()'s
        /*misc_flags=*/plink2::kfMisc0,
        /*import_flags=*/plink2::kfImportKeepAutoconv,
        /*load_filter_log_import_flags=*/plink2::kfLoadFilterLog0,
        /*oxford_import_flags=*/plink2::kfOxfordImport0,  // plink2.cc default when no ref-first/-last/-unknown modifier is given (see this file's top comment)
        /*psam_01=*/0,       // plink2.cc default: no --1
        /*is_splitpar=*/0,   // no --split-par
        /*is_sortvars=*/0,   // plink2.cc default: pc.sort_vars_mode starts at kSort0 (<= kSortNone)
        /*hard_call_thresh=*/UINT32_MAX,  // plink2.cc default: OxGenToPgen() substitutes kDosageMid / 10 itself
        /*dosage_erase_thresh=*/0,        // plink2.cc default
        /*import_dosage_certainty=*/0.0,  // plink2.cc default
        /*id_delim=*/'\0',                // plink2.cc default: no --id-delim
        /*overlong_varids_mode=*/plink2::kImportOverlongVarIds0,  // plink2.cc default
        /*max_thread_ct=*/1,  // conservative single-threaded default for a library call
        outname, outname_end, &chr_info);
    if (reterr != plink2::kPglRetSuccess) {
      snprintf(errbuf, errbuf_len, "OxGenToPgen(\"%s\") failed: %s (code %d)",
               gen_path, rpgen_reterr_str(reterr), static_cast<int>(reterr));
      goto cleanup;
    }
  }

  rc = 0;

cleanup:
  if (chr_info_ready) {
    plink2::CleanupChrInfo(&chr_info);
  }
  if (bigstack_ua) {
    free(bigstack_ua);
  }
  plink2::g_bigstack_base = nullptr;
  plink2::g_bigstack_end = nullptr;
  return rc;
}

// Converts bgenname/samplename to out_pgen_path via plink2's own
// OxBgenToPgen(). Unlike .gen, a BGEN v1.2/v1.3 file may carry its own
// sample identifier block, so sample_path may legitimately be a *pointer to
// an empty string* here - never nullptr: OxBgenToPgen() unconditionally
// evaluates `samplename[0]` (see plink2_import.cc) to decide whether an
// external .sample file was given, so a null samplename would crash instead
// of falling back to the .bgen's own sample IDs. rpgen_import_bgen() (the
// extern "C" wrapper below) is the one that turns a null sample_path into
// "" before calling this function - this function itself requires a
// non-null (possibly empty) C string, same discipline pgenlib's own
// possibly-absent-string parameters use elsewhere in this package.
int rpgen_import_bgen(const char *bgen_path, const char *sample_path,
                       const char *out_pgen_path, char *errbuf,
                       size_t errbuf_len) {
  if (errbuf_len > 0) {
    errbuf[0] = '\0';
  }

  int rc = -1;
  unsigned char *volatile bigstack_ua = nullptr;
  volatile bool chr_info_ready = false;
  plink2::ChrInfo chr_info;
  char outname[plink2::kPglFnamesize];
  char *outname_end = nullptr;

  if (rpgen_split_pgen_outname(out_pgen_path, outname, sizeof(outname),
                                &outname_end, errbuf, errbuf_len) != 0) {
    return -1;
  }

  if (setjmp(rpgen_plink2_exit_jmp) != 0) {
    if (rpgen_plink2_aborted) {
      snprintf(errbuf, errbuf_len,
               "OxBgenToPgen(\"%s\") aborted (internal plink2 abort())",
               bgen_path);
    } else {
      snprintf(errbuf, errbuf_len,
               "OxBgenToPgen(\"%s\") called exit(%d) internally", bgen_path,
               rpgen_plink2_exit_code);
    }
    goto cleanup;
  }

  {
    uintptr_t malloc_mib_final = 0;
    unsigned char *bigstack_ua_tmp = nullptr;
    if (plink2::InitBigstack(kRpgenImportArenaMib, &malloc_mib_final,
                              &bigstack_ua_tmp) != plink2::kPglRetSuccess) {
      snprintf(errbuf, errbuf_len,
               "failed to allocate a %lu MiB working arena",
               static_cast<unsigned long>(kRpgenImportArenaMib));
      goto cleanup;
    }
    bigstack_ua = bigstack_ua_tmp;
  }

  if (plink2::InitChrInfoHuman(&chr_info) != plink2::kPglRetSuccess) {
    snprintf(errbuf, errbuf_len, "failed to initialize chromosome info");
    goto cleanup;
  }
  chr_info_ready = true;

  {
    const plink2::PglErr reterr = plink2::OxBgenToPgen(
        bgen_path, sample_path,
        /*const_fid=*/nullptr,          // plink2.cc default: no constant family ID
        /*ox_single_chr_str=*/nullptr,  // plink2.cc default: no --oxford-single-chr (BGEN variant blocks carry their own chr)
        /*ox_missing_code=*/nullptr,    // plink2.cc default: no --missing-code (falls back to "NA" if a .sample is used)
        /*missing_catname=*/"NONE",     // plink2.cc default
        /*missing_varid=*/".",          // plink2.cc default missing_varid, same as VcfToPgen()'s
        /*misc_flags=*/plink2::kfMisc0,
        /*import_flags=*/plink2::kfImportKeepAutoconv,
        /*load_filter_log_import_flags=*/plink2::kfLoadFilterLog0,
        /*oxford_import_flags=*/plink2::kfOxfordImport0,  // plink2.cc default when no ref-first/-last/-unknown modifier is given (see this file's top comment)
        /*psam_01=*/0,                  // plink2.cc default: no --1
        /*is_update_or_impute_sex=*/0,  // no --update-sex/--impute-sex
        /*is_splitpar=*/0,              // no --split-par
        /*is_sortvars=*/0,              // plink2.cc default
        /*hard_call_thresh=*/UINT32_MAX,  // plink2.cc default: OxBgenToPgen() substitutes kDosageMid / 10 itself
        /*dosage_erase_thresh=*/0,        // plink2.cc default
        /*import_dosage_certainty=*/0.0,  // plink2.cc default
        /*id_delim=*/'\0',                // plink2.cc default: no --id-delim
        /*idspace_to=*/'\0',              // plink2.cc default
        /*import_max_allele_ct=*/0x7ffffffe,  // plink2.cc default (effectively unlimited)
        /*overlong_varids_mode=*/plink2::kImportOverlongVarIds0,  // plink2.cc default
        /*max_thread_ct=*/1,  // conservative single-threaded default for a library call
        outname, outname_end, &chr_info);
    if (reterr != plink2::kPglRetSuccess) {
      snprintf(errbuf, errbuf_len,
               "OxBgenToPgen(\"%s\") failed: %s (code %d)", bgen_path,
               rpgen_reterr_str(reterr), static_cast<int>(reterr));
      goto cleanup;
    }
  }

  rc = 0;

cleanup:
  if (chr_info_ready) {
    plink2::CleanupChrInfo(&chr_info);
  }
  if (bigstack_ua) {
    free(bigstack_ua);
  }
  plink2::g_bigstack_base = nullptr;
  plink2::g_bigstack_end = nullptr;
  return rc;
}

// Converts hapsname/legendname/samplename to out_pgen_path via plink2's own
// OxHapslegendToPgen(). A .haps/.legend pair encodes *phased* haplotypes -
// the produced .pgen carries that phase information, but Rpgen's existing
// readers (rpgen_read_hardcalls()/rpgen_read_dosages(), both plain
// PgrGet()/PgrGetD() collapsed-hardcall reads) do not read phase back yet;
// that is a future milestone. This driver still produces a fully valid
// .pgen - only the *reading* of its phase bits is unimplemented, which is
// why inst/tinytest/test_import_matrix.R checks this format at the hardcall
// level only (see that file's comment on the .haps section).
//
// `chr` is required, unlike every other string argument in this file's
// drivers that plink2.cc defaults to nullptr: the classic IMPUTE2 .legend
// format (id/position/a0/a1, no chromosome column - see this driver's own
// tinytest fixture) carries no chromosome of its own, so plink2's CLI
// itself requires a chromosome code argument whenever --legend is used
// (`--legend <filename> <chr code>`, confirmed via `plink2 --help legend`).
// This is not merely a CLI convenience: OxHapslegendToPgen() unconditionally
// calls InitOxfordSingleChr(ox_single_chr_str, ...) whenever legendname[0]
// is set (plink2_import.cc), and InitOxfordSingleChr() dereferences
// ox_single_chr_str without a null check - passing nullptr here segfaults
// the whole R session instead of returning a PglErr. Found by testing this
// driver against a real .legend fixture (see
// inst/tinytest/test_import_matrix.R's header comment); `chr` is threaded
// through from R/rpgen_import.R's rpgen_import_haps() as a consequence.
int rpgen_import_haps(const char *haps_path, const char *legend_path,
                       const char *sample_path, const char *chr,
                       const char *out_pgen_path, char *errbuf,
                       size_t errbuf_len) {
  if (errbuf_len > 0) {
    errbuf[0] = '\0';
  }

  int rc = -1;
  unsigned char *volatile bigstack_ua = nullptr;
  volatile bool chr_info_ready = false;
  plink2::ChrInfo chr_info;
  char outname[plink2::kPglFnamesize];
  char *outname_end = nullptr;

  if (rpgen_split_pgen_outname(out_pgen_path, outname, sizeof(outname),
                                &outname_end, errbuf, errbuf_len) != 0) {
    return -1;
  }

  if (setjmp(rpgen_plink2_exit_jmp) != 0) {
    if (rpgen_plink2_aborted) {
      snprintf(errbuf, errbuf_len,
               "OxHapslegendToPgen(\"%s\") aborted (internal plink2 abort())",
               haps_path);
    } else {
      snprintf(errbuf, errbuf_len,
               "OxHapslegendToPgen(\"%s\") called exit(%d) internally",
               haps_path, rpgen_plink2_exit_code);
    }
    goto cleanup;
  }

  {
    uintptr_t malloc_mib_final = 0;
    unsigned char *bigstack_ua_tmp = nullptr;
    if (plink2::InitBigstack(kRpgenImportArenaMib, &malloc_mib_final,
                              &bigstack_ua_tmp) != plink2::kPglRetSuccess) {
      snprintf(errbuf, errbuf_len,
               "failed to allocate a %lu MiB working arena",
               static_cast<unsigned long>(kRpgenImportArenaMib));
      goto cleanup;
    }
    bigstack_ua = bigstack_ua_tmp;
  }

  if (plink2::InitChrInfoHuman(&chr_info) != plink2::kPglRetSuccess) {
    snprintf(errbuf, errbuf_len, "failed to initialize chromosome info");
    goto cleanup;
  }
  chr_info_ready = true;

  {
    // Only ever *set* (never cleared) by OxHapslegendToPgen() when it writes
    // a separate .pgi index file alongside the .pgen - which, with
    // kfImportKeepAutoconv passed below (keep_pgi = !(import_flags &
    // kfImportKeepAutoconv) in plink2_import.cc), it will not do; this
    // driver always produces a plain self-contained .pgen, like every other
    // importer in this file. Initialized to 0 to match plink2.cc's own call
    // site.
    uint32_t pgi_generated = 0;
    const plink2::PglErr reterr = plink2::OxHapslegendToPgen(
        haps_path, legend_path, sample_path,
        /*const_fid=*/nullptr,  // plink2.cc default: no constant family ID
        /*ox_single_chr_str=*/chr,  // required here - see this function's comment; the classic .legend format has no chr column of its own
        /*ox_missing_code=*/nullptr,    // plink2.cc default: falls back to "NA"
        /*missing_catname=*/"NONE",     // plink2.cc default
        /*missing_varid=*/".",          // plink2.cc default missing_varid, same as VcfToPgen()'s
        /*misc_flags=*/plink2::kfMisc0,
        /*import_flags=*/plink2::kfImportKeepAutoconv,
        /*load_filter_log_import_flags=*/plink2::kfLoadFilterLog0,
        /*oxford_import_flags=*/plink2::kfOxfordImport0,  // plink2.cc default when no ref-first/-last/-unknown modifier is given (see this file's top comment)
        /*psam_01=*/0,                  // plink2.cc default: no --1
        /*is_update_or_impute_sex=*/0,  // no --update-sex/--impute-sex
        /*is_splitpar=*/0,              // no --split-par
        /*is_sortvars=*/0,              // plink2.cc default
        /*id_delim=*/'\0',              // plink2.cc default: no --id-delim
        /*overlong_varids_mode=*/plink2::kImportOverlongVarIds0,  // plink2.cc default
        /*max_thread_ct=*/1,  // conservative single-threaded default for a library call
        outname, outname_end, &chr_info, &pgi_generated);
    if (reterr != plink2::kPglRetSuccess) {
      snprintf(errbuf, errbuf_len,
               "OxHapslegendToPgen(\"%s\") failed: %s (code %d)", haps_path,
               rpgen_reterr_str(reterr), static_cast<int>(reterr));
      goto cleanup;
    }
    (void)pgi_generated;
  }

  rc = 0;

cleanup:
  if (chr_info_ready) {
    plink2::CleanupChrInfo(&chr_info);
  }
  if (bigstack_ua) {
    free(bigstack_ua);
  }
  plink2::g_bigstack_base = nullptr;
  plink2::g_bigstack_end = nullptr;
  return rc;
}

// Converts dosage_path/fam_path/map_path to out_pgen_path via plink2's own
// Plink1DosageToPgen() (the legacy PLINK 1.x `--import-dosage` text format:
// one header line of FID/IID pairs, then one row per variant of
// "ID A1 A2 <per-sample value(s)>"). pdip's fields below mirror
// InitPlink1Dosage()'s own zero-initialization - the same defaults a plain
// `--import-dosage <path>` (no modifiers) leaves in place - which in turn
// makes Plink1DosageToPgen() *infer* the per-sample column format (single
// dosage value, or a double/triple-probability triple) from the data row's
// own column count (see plink2_import.cc's `format_infer` local), rather
// than this driver hard-coding an assumption. A1 is imported as the ALT
// allele and A2 as REF (prov_ref_allele_second, same convention as the
// Oxford drivers above when no ref-first modifier is requested).
int rpgen_import_plink1_dosage(const char *dosage_path, const char *fam_path,
                                const char *map_path,
                                const char *out_pgen_path, char *errbuf,
                                size_t errbuf_len) {
  if (errbuf_len > 0) {
    errbuf[0] = '\0';
  }

  int rc = -1;
  unsigned char *volatile bigstack_ua = nullptr;
  volatile bool chr_info_ready = false;
  plink2::ChrInfo chr_info;
  char outname[plink2::kPglFnamesize];
  char *outname_end = nullptr;

  if (rpgen_split_pgen_outname(out_pgen_path, outname, sizeof(outname),
                                &outname_end, errbuf, errbuf_len) != 0) {
    return -1;
  }

  if (setjmp(rpgen_plink2_exit_jmp) != 0) {
    if (rpgen_plink2_aborted) {
      snprintf(
          errbuf, errbuf_len,
          "Plink1DosageToPgen(\"%s\") aborted (internal plink2 abort())",
          dosage_path);
    } else {
      snprintf(errbuf, errbuf_len,
               "Plink1DosageToPgen(\"%s\") called exit(%d) internally",
               dosage_path, rpgen_plink2_exit_code);
    }
    goto cleanup;
  }

  {
    uintptr_t malloc_mib_final = 0;
    unsigned char *bigstack_ua_tmp = nullptr;
    if (plink2::InitBigstack(kRpgenImportArenaMib, &malloc_mib_final,
                              &bigstack_ua_tmp) != plink2::kPglRetSuccess) {
      snprintf(errbuf, errbuf_len,
               "failed to allocate a %lu MiB working arena",
               static_cast<unsigned long>(kRpgenImportArenaMib));
      goto cleanup;
    }
    bigstack_ua = bigstack_ua_tmp;
  }

  if (plink2::InitChrInfoHuman(&chr_info) != plink2::kPglRetSuccess) {
    snprintf(errbuf, errbuf_len, "failed to initialize chromosome info");
    goto cleanup;
  }
  chr_info_ready = true;

  {
    plink2::Plink1DosageInfo pdip;
    plink2::InitPlink1Dosage(&pdip);  // flags=0 (format inferred), skips={0,0,0}, chr/pos_col_idx=UINT32_MAX, id_delim='\0' - see comment above
    const plink2::PglErr reterr = plink2::Plink1DosageToPgen(
        dosage_path, fam_path, map_path,
        /*import_single_chr_str=*/nullptr,  // map_path is mandatory here (see R/rpgen_import.R), so no single-chr fallback is needed
        &pdip,
        /*missing_catname=*/"NONE",  // plink2.cc default
        /*misc_flags=*/plink2::kfMisc0,
        /*import_flags=*/plink2::kfImportKeepAutoconv,
        /*load_filter_log_import_flags=*/plink2::kfLoadFilterLog0,
        /*psam_01=*/0,                       // plink2.cc default: no --1
        /*fam_cols=*/plink2::kfFamCol13456,  // plink2.cc default (all .fam columns)
        /*missing_pheno=*/-9,                // plink2.cc default
        /*hard_call_thresh=*/UINT32_MAX,  // plink2.cc default: Plink1DosageToPgen() substitutes kDosageMid / 10 itself
        /*dosage_erase_thresh=*/0,        // plink2.cc default
        /*import_dosage_certainty=*/0.0,  // plink2.cc default
        /*max_thread_ct=*/1,  // conservative single-threaded default for a library call
        outname, outname_end, &chr_info);
    if (reterr != plink2::kPglRetSuccess) {
      snprintf(errbuf, errbuf_len,
               "Plink1DosageToPgen(\"%s\") failed: %s (code %d)", dosage_path,
               rpgen_reterr_str(reterr), static_cast<int>(reterr));
      goto cleanup;
    }
  }

  rc = 0;

cleanup:
  if (chr_info_ready) {
    plink2::CleanupChrInfo(&chr_info);
  }
  if (bigstack_ua) {
    free(bigstack_ua);
  }
  plink2::g_bigstack_base = nullptr;
  plink2::g_bigstack_end = nullptr;
  return rc;
}

}  // namespace

extern "C" {

// Shared argument validation for this file's RC_rpgen_import_*() wrappers:
// `sexp` must be a single non-NA string; `argname` names it in the error
// message. Mirrors rpgen.cpp's own rpgen_check_single_string().
static const char *rpgen_import_check_string(SEXP sexp, const char *argname) {
  if (TYPEOF(sexp) != STRSXP || Rf_length(sexp) != 1 ||
      STRING_ELT(sexp, 0) == NA_STRING) {
    Rf_error("%s must be a single non-NA string", argname);
  }
  return CHAR(STRING_ELT(sexp, 0));
}

// Same, but `sexp` may also be R_NilValue (NULL), in which case this returns
// nullptr - used only by RC_rpgen_import_bgen()'s optional `sample`
// argument. Note this is a distinct contract from
// rpgen_import_bgen()/Rpgen_import_bgen()'s own sample_path parameter below,
// which must never be nullptr (see rpgen_import_bgen()'s comment); the
// nullptr-to-"" translation happens in Rpgen_import_bgen(), not here.
static const char *rpgen_import_check_string_or_null(SEXP sexp,
                                                       const char *argname) {
  if (sexp == R_NilValue) {
    return nullptr;
  }
  return rpgen_import_check_string(sexp, argname);
}

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

// See inst/include/Rpgen.h's Rpgen_import_bcf_fun doc comment for the full
// contract; this just forwards to the implementation above.
int Rpgen_import_bcf(const char *bcf_path, const char *out_pgen_path,
                      char *errbuf, size_t errbuf_len) {
  return rpgen_import_bcf(bcf_path, out_pgen_path, errbuf, errbuf_len);
}

SEXP RC_rpgen_import_bcf(SEXP bcf_sexp, SEXP out_sexp) {
  const char *bcf_path = rpgen_import_check_string(bcf_sexp, "bcf");
  const char *out_pgen_path = rpgen_import_check_string(out_sexp, "out");

  char errbuf[512];
  if (Rpgen_import_bcf(bcf_path, out_pgen_path, errbuf, sizeof(errbuf)) != 0) {
    Rf_error("rpgen_import_bcf(\"%s\") failed: %s", bcf_path, errbuf);
  }
  return Rf_mkString(out_pgen_path);
}

// See inst/include/Rpgen.h's Rpgen_import_gen_fun doc comment for the full
// contract; this just forwards to the implementation above. Unlike
// Rpgen_import_bgen() below, sample_path here must be non-null (a .gen file
// has no sample IDs of its own - see rpgen_import_gen()'s comment).
int Rpgen_import_gen(const char *gen_path, const char *sample_path,
                      const char *out_pgen_path, char *errbuf,
                      size_t errbuf_len) {
  return rpgen_import_gen(gen_path, sample_path, out_pgen_path, errbuf,
                           errbuf_len);
}

SEXP RC_rpgen_import_gen(SEXP gen_sexp, SEXP sample_sexp, SEXP out_sexp) {
  const char *gen_path = rpgen_import_check_string(gen_sexp, "gen");
  const char *sample_path = rpgen_import_check_string(sample_sexp, "sample");
  const char *out_pgen_path = rpgen_import_check_string(out_sexp, "out");

  char errbuf[512];
  if (Rpgen_import_gen(gen_path, sample_path, out_pgen_path, errbuf,
                        sizeof(errbuf)) != 0) {
    Rf_error("rpgen_import_gen(\"%s\") failed: %s", gen_path, errbuf);
  }
  return Rf_mkString(out_pgen_path);
}

// See inst/include/Rpgen.h's Rpgen_import_bgen_fun doc comment for the full
// contract; this just forwards to the implementation above, after turning a
// null sample_path (R's `sample = NULL`, meaning "use the .bgen's own
// embedded sample identifiers, if any") into an empty C string - see
// rpgen_import_bgen()'s comment in the anonymous namespace above for why a
// literal nullptr is not safe to pass into OxBgenToPgen() itself.
int Rpgen_import_bgen(const char *bgen_path, const char *sample_path,
                       const char *out_pgen_path, char *errbuf,
                       size_t errbuf_len) {
  return rpgen_import_bgen(bgen_path, sample_path ? sample_path : "",
                            out_pgen_path, errbuf, errbuf_len);
}

SEXP RC_rpgen_import_bgen(SEXP bgen_sexp, SEXP sample_sexp, SEXP out_sexp) {
  const char *bgen_path = rpgen_import_check_string(bgen_sexp, "bgen");
  const char *sample_path =
      rpgen_import_check_string_or_null(sample_sexp, "sample");
  const char *out_pgen_path = rpgen_import_check_string(out_sexp, "out");

  char errbuf[512];
  if (Rpgen_import_bgen(bgen_path, sample_path, out_pgen_path, errbuf,
                         sizeof(errbuf)) != 0) {
    Rf_error("rpgen_import_bgen(\"%s\") failed: %s", bgen_path, errbuf);
  }
  return Rf_mkString(out_pgen_path);
}

// See inst/include/Rpgen.h's Rpgen_import_haps_fun doc comment for the full
// contract; this just forwards to the implementation above. `chr` must be
// non-null - see rpgen_import_haps()'s comment in the anonymous namespace
// above for why (a bare nullptr passed through to OxHapslegendToPgen()
// segfaults instead of erroring cleanly).
int Rpgen_import_haps(const char *haps_path, const char *legend_path,
                       const char *sample_path, const char *chr,
                       const char *out_pgen_path, char *errbuf,
                       size_t errbuf_len) {
  return rpgen_import_haps(haps_path, legend_path, sample_path, chr,
                            out_pgen_path, errbuf, errbuf_len);
}

SEXP RC_rpgen_import_haps(SEXP haps_sexp, SEXP legend_sexp, SEXP sample_sexp,
                           SEXP chr_sexp, SEXP out_sexp) {
  const char *haps_path = rpgen_import_check_string(haps_sexp, "haps");
  const char *legend_path = rpgen_import_check_string(legend_sexp, "legend");
  const char *sample_path = rpgen_import_check_string(sample_sexp, "sample");
  const char *chr = rpgen_import_check_string(chr_sexp, "chr");
  const char *out_pgen_path = rpgen_import_check_string(out_sexp, "out");

  char errbuf[512];
  if (Rpgen_import_haps(haps_path, legend_path, sample_path, chr,
                         out_pgen_path, errbuf, sizeof(errbuf)) != 0) {
    Rf_error("rpgen_import_haps(\"%s\") failed: %s", haps_path, errbuf);
  }
  return Rf_mkString(out_pgen_path);
}

// See inst/include/Rpgen.h's Rpgen_import_plink1_dosage_fun doc comment for
// the full contract; this just forwards to the implementation above.
int Rpgen_import_plink1_dosage(const char *dosage_path, const char *fam_path,
                                const char *map_path,
                                const char *out_pgen_path, char *errbuf,
                                size_t errbuf_len) {
  return rpgen_import_plink1_dosage(dosage_path, fam_path, map_path,
                                     out_pgen_path, errbuf, errbuf_len);
}

SEXP RC_rpgen_import_plink1_dosage(SEXP dosage_sexp, SEXP fam_sexp,
                                    SEXP map_sexp, SEXP out_sexp) {
  const char *dosage_path = rpgen_import_check_string(dosage_sexp, "dosage");
  const char *fam_path = rpgen_import_check_string(fam_sexp, "fam");
  const char *map_path = rpgen_import_check_string(map_sexp, "map");
  const char *out_pgen_path = rpgen_import_check_string(out_sexp, "out");

  char errbuf[512];
  if (Rpgen_import_plink1_dosage(dosage_path, fam_path, map_path,
                                  out_pgen_path, errbuf, sizeof(errbuf)) !=
      0) {
    Rf_error("rpgen_import_plink1_dosage(\"%s\") failed: %s", dosage_path,
             errbuf);
  }
  return Rf_mkString(out_pgen_path);
}

}  // extern "C"
