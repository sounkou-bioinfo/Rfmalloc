/*
 * rpgen.cpp - Rpgen's own C-callable surface over vendored PLINK2 pgenlib.
 *
 * This file is hand-authored, not vendored: it is not touched by
 * tools/vendor-pgenlib/vendorpgen.R. It is the reason Rpgen exists as a
 * separate package from pgenlibr - pgenlibr exposes an R-level (Rcpp) API
 * only, no C-callable one, so nothing else can link against its pgenlib
 * build. Rpgen vendors the same pgenlib read subset (see PROVENANCE.md) and
 * registers a small C API with R_RegisterCCallable() so sibling packages
 * (Rfmalloc et al.) can eventually read .pgen genotypes natively.
 *
 * Milestone 1 scope: open a .pgen file far enough to learn its sample and
 * variant counts, then close it again. This exercises the exact same
 * PgfiInitPhase1 / PgfiInitPhase2 / PgrInit sequence a real reader needs
 * (see rpgen_open_info() below, modeled closely on pgenlibr's own
 * RPgenReader::Load()/Close() in src/pgenlibr.cpp of the vendored tarball),
 * without yet keeping a live reader handle around between R calls - later
 * milestones add that.
 */

#include <cstdint>
#include <cstdio>

#include <R.h>
#include <Rinternals.h>
#include <R_ext/Rdynload.h>

#include "include/pgenlib_read.h"
#include "include/pvar_ffi_support.h"  // RefcountedWptr, for allele_idx_offsets

// Internal version constant for the C-callable API registered below. Not
// shared with the installed inst/include/Rpgen.h: downstream consumers read
// the version at runtime through Rpgen_api_version(), the same pattern
// Rggml's RGGML_API_VERSION uses (see packages/Rggml/src/rggml_api.h).
#define RPGEN_API_VERSION 1

namespace {

// Open fname just far enough to read PgenFileInfo's header counts, then
// close everything again. Mirrors pgenlibr's RPgenReader::Load() up through
// PgrInit() (we stop there - no genotype-read buffers are allocated, since
// milestone 1 has no genotype reader yet) and its Close(), including that
// function's exact cleanup order.
//
// Single-exit cleanup via goto: every resource acquired below (the
// allele_idx_offsets refcounted block, pgfi_alloc, pgr_alloc, the open
// FILE*) is released exactly once at the `cleanup` label, regardless of
// which step failed, instead of duplicating free()s on every early return.
// That relies on two aliasing facts pgenlibr.cpp's own Close() also
// depends on:
//   - info.vrtypes, when set, IS the base pointer of pgfi_alloc: it is
//     carved as the very first allocation from that arena
//     (pgenlib_read.cc: "unsigned char* vrtypes_iter = pgfi_alloc;
//     pgfip->vrtypes = vrtypes_iter;"), so aligned_free(info.vrtypes) is
//     aligned_free(pgfi_alloc). If PgfiInitPhase2() fails before reaching
//     that assignment, info.vrtypes is still nullptr (PgfiInitPhase1()
//     unconditionally resets it), so we fall back to freeing pgfi_alloc
//     directly - either way it is freed exactly once.
//   - PgrGetFreadBuf(&state), once PgrInit() reaches its own first
//     fread_buf assignment (its only actions before that point, opening
//     and seeking the file, are also its only failure points - everything
//     after is unconditional arena carving that cannot fail), IS the base
//     pointer of pgr_alloc, for the same reason. If PgrInit() fails before
//     that assignment, PgrGetFreadBuf() is still nullptr (we initialize it
//     so via PgrSetFreadBuf(nullptr, ...) before calling it), so we again
//     fall back to freeing pgr_alloc directly.
int rpgen_open_info(const char *fname, uint32_t *n_sample_out,
                     uint32_t *n_variant_out, char *errbuf,
                     size_t errbuf_len) {
  if (errbuf_len > 0) {
    errbuf[0] = '\0';
  }

  int rc = -1;
  plink2::PgenFileInfo info;
  plink2::PreinitPgfi(&info);
  plink2::PgenReader state;
  plink2::PreinitPgr(&state);
  plink2::PgrSetFreadBuf(nullptr, &state);
  plink2::RefcountedWptr *allele_idx_offsetsp = nullptr;
  unsigned char *pgfi_alloc = nullptr;
  unsigned char *pgr_alloc = nullptr;

  const uint32_t cur_sample_ct = UINT32_MAX;
  const uint32_t cur_variant_ct = UINT32_MAX;
  plink2::PgenHeaderCtrl header_ctrl;
  uintptr_t pgfi_alloc_cacheline_ct;
  char pgenlib_errbuf[plink2::kPglErrstrBufBlen];

  if (plink2::PgfiInitPhase1(fname, nullptr, cur_variant_ct, cur_sample_ct,
                              &header_ctrl, &info, &pgfi_alloc_cacheline_ct,
                              pgenlib_errbuf) != plink2::kPglRetSuccess) {
    // pgenlib error strings are "Error: <msg>.\n"; pgenlibr.cpp skips the
    // 7-char "Error: " prefix before surfacing the message, we do the same.
    snprintf(errbuf, errbuf_len, "%s", &pgenlib_errbuf[7]);
    goto cleanup;
  }

  {
    const uint32_t raw_variant_ct = info.raw_variant_ct;

    // header_ctrl & 0x30: file stores per-variant allele counts, i.e. some
    // variant is multiallelic. PgfiInitPhase2Ex() requires
    // info.allele_idx_offsets to already be allocated in that case (it
    // errors out otherwise); we don't care about the actual counts for
    // milestone 1, only that Phase2 succeeds. No pvar is supplied, so we
    // allocate our own scratch block exactly as pgenlibr's Load() does
    // when it has no pvar object to source one from.
    if (header_ctrl & 0x30) {
      allele_idx_offsetsp = plink2::CreateRefcountedWptr(raw_variant_ct + 1);
      if (!allele_idx_offsetsp) {
        snprintf(errbuf, errbuf_len, "out of memory (allele_idx_offsets)");
        goto cleanup;
      }
      info.allele_idx_offsets = allele_idx_offsetsp->p;
      // info.max_allele_ct is set by PgfiInitPhase2() itself in this case.
    } else {
      info.max_allele_ct = 2;
    }

    if (plink2::cachealigned_malloc(
            pgfi_alloc_cacheline_ct * plink2::kCacheline, &pgfi_alloc)) {
      snprintf(errbuf, errbuf_len, "out of memory (pgfi_alloc)");
      goto cleanup;
    }

    uint32_t max_vrec_width;
    uintptr_t pgr_alloc_cacheline_ct;
    if (plink2::PgfiInitPhase2(header_ctrl, 1, 0, 0, 0, raw_variant_ct,
                                &max_vrec_width, &info, pgfi_alloc,
                                &pgr_alloc_cacheline_ct, pgenlib_errbuf)) {
      snprintf(errbuf, errbuf_len, "%s", &pgenlib_errbuf[7]);
      goto cleanup;
    }

    if (plink2::cachealigned_malloc(
            pgr_alloc_cacheline_ct * plink2::kCacheline, &pgr_alloc)) {
      snprintf(errbuf, errbuf_len, "out of memory (pgr_alloc)");
      goto cleanup;
    }

    const plink2::PglErr reterr =
        plink2::PgrInit(fname, max_vrec_width, &info, &state, pgr_alloc);
    if (reterr != plink2::kPglRetSuccess) {
      snprintf(errbuf, errbuf_len, "PgrInit() error %d",
               static_cast<int>(reterr));
      goto cleanup;
    }

    *n_sample_out = info.raw_sample_ct;
    *n_variant_out = info.raw_variant_ct;
    rc = 0;
  }

cleanup:
  {
    plink2::PglErr cleanup_reterr = plink2::kPglRetSuccess;
    plink2::CleanupPgr(&state, &cleanup_reterr);
  }
  if (plink2::PgrGetFreadBuf(&state)) {
    plink2::aligned_free(plink2::PgrGetFreadBuf(&state));
  } else if (pgr_alloc) {
    plink2::aligned_free(pgr_alloc);
  }

  {
    plink2::PglErr cleanup_reterr = plink2::kPglRetSuccess;
    plink2::CleanupPgfi(&info, &cleanup_reterr);
  }
  if (info.vrtypes) {
    plink2::aligned_free(info.vrtypes);
  } else if (pgfi_alloc) {
    plink2::aligned_free(pgfi_alloc);
  }
  plink2::CondReleaseRefcountedWptr(&allele_idx_offsetsp);

  return rc;
}

}  // namespace

extern "C" {

int Rpgen_api_version(void) { return RPGEN_API_VERSION; }

int Rpgen_open_info(const char *path, uint32_t *n_sample_out,
                     uint32_t *n_variant_out, char *errbuf,
                     size_t errbuf_len) {
  return rpgen_open_info(path, n_sample_out, n_variant_out, errbuf,
                          errbuf_len);
}

SEXP RC_rpgen_info(SEXP path_sexp) {
  if (TYPEOF(path_sexp) != STRSXP || Rf_length(path_sexp) != 1 ||
      STRING_ELT(path_sexp, 0) == NA_STRING) {
    Rf_error("path must be a single non-NA string");
  }
  const char *path = CHAR(STRING_ELT(path_sexp, 0));

  uint32_t n_sample = 0;
  uint32_t n_variant = 0;
  char errbuf[512];
  if (Rpgen_open_info(path, &n_sample, &n_variant, errbuf, sizeof(errbuf)) !=
      0) {
    Rf_error("Rpgen_open_info(\"%s\") failed: %s", path, errbuf);
  }

  SEXP result = PROTECT(Rf_allocVector(VECSXP, 2));
  SEXP names = PROTECT(Rf_allocVector(STRSXP, 2));
  SET_STRING_ELT(names, 0, Rf_mkChar("n_sample"));
  SET_STRING_ELT(names, 1, Rf_mkChar("n_variant"));
  SET_VECTOR_ELT(result, 0, Rf_ScalarInteger(static_cast<int>(n_sample)));
  SET_VECTOR_ELT(result, 1, Rf_ScalarInteger(static_cast<int>(n_variant)));
  Rf_setAttrib(result, R_NamesSymbol, names);
  UNPROTECT(2);
  return result;
}

static const R_CallMethodDef CallEntries[] = {
    {"RC_rpgen_info", (DL_FUNC)&RC_rpgen_info, 1},
    {NULL, NULL, 0}};

static void register_c_callables(DllInfo *dll) {
  R_RegisterCCallable("Rpgen", "Rpgen_api_version",
                       (DL_FUNC)Rpgen_api_version);
  R_RegisterCCallable("Rpgen", "Rpgen_open_info", (DL_FUNC)Rpgen_open_info);
  (void)dll;
}

void R_init_Rpgen(DllInfo *dll) {
  R_registerRoutines(dll, NULL, CallEntries, NULL, NULL);
  register_c_callables(dll);
  R_useDynamicSymbols(dll, FALSE);
}

}  // extern "C"
