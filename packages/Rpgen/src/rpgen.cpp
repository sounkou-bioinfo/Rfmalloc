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
 * without yet keeping a live reader handle around between R calls.
 *
 * Milestone 2 scope: read genotypes. rpgen_read_hardcalls()/
 * rpgen_read_dosages() below extend rpgen_open_info()'s setup all the way to
 * a working plink2::PgrGet()/PgrGetD() loop over every variant, for every
 * sample (no subsetting), producing a dense samples x variants matrix per
 * call. Still no persistent handle across R calls - each call opens the file,
 * reads its requested variant range, and closes it again, matching milestone
 * 1's style. See the RpgenFullReader comment below for the buffer layout,
 * modeled on pgenlibr's RPgenReader::Load()/ReadIntList()/ReadList().
 */

#include <climits>
#include <cstdint>
#include <cstdio>

#include <R.h>
#include <Rinternals.h>
#include <R_ext/Rdynload.h>

#include "include/pgenlib_ffi_support.h"  // Dosage16ToDoubles, GenoarrLookup*
#include "include/pgenlib_read.h"
#include "include/pvar_ffi_support.h"  // RefcountedWptr, for allele_idx_offsets

// plink2's headers #define FALSE/TRUE as int macros, which shadow R's Rboolean
// enumerators; mingw's stricter compiler then rejects R_useDynamicSymbols(dll,
// FALSE) as an int-to-Rboolean conversion. Undo the macros so FALSE/TRUE resolve
// to R's enumerators again (rpgen.cpp uses them only for the R API).
#ifdef FALSE
#  undef FALSE
#endif
#ifdef TRUE
#  undef TRUE
#endif

// Internal version constant for the C-callable API registered below. Not
// shared with the installed inst/include/Rpgen.h: downstream consumers read
// the version at runtime through Rpgen_api_version(), the same pattern
// Rggml's RGGML_API_VERSION uses (see packages/Rggml/src/rggml_api.h).
//
// Bumped to 2 in milestone 2: adds Rpgen_read_hardcalls()/
// Rpgen_read_dosages() alongside milestone 1's Rpgen_open_info().
#define RPGEN_API_VERSION 2

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

// -- milestone 2: genotype reading ------------------------------------------
//
// Lookup tables PgrGet()/PgrGetD() results are decoded through. Byte-for-byte
// the same tables pgenlibr.cpp defines for RPgenReader::ReadIntHardcalls()/
// ReadHardcalls() (see kGenoRInt32Quads/kGenoRDoublePairs there): genovec's
// 2-bit codes are {0, 1, 2, 3}, meaning {hom-ref, het, hom-alt, missing} for
// a biallelic variant, or {hom-ref, has-one-non-ref, has-two-non-ref,
// missing} in the multiallelic case (ALT alleles collapsed together, exactly
// what a plain PgrGet()/PgrGetD() call - as opposed to the allele-specific
// PgrGet1()/PgrGet1D() - already returns without needing a .pvar's allele
// bookkeeping; see rpgen_read_hardcalls()'s comment below).
static const int32_t kGenoRInt32Quads[1024] ALIGNV16 =
    QUAD_TABLE256(0, 1, 2, NA_INTEGER);
static const double kGenoRDoublePairs[32] ALIGNV16 =
    PAIR_TABLE16(0.0, 1.0, 2.0, NA_REAL);

// A fully-opened, all-samples reader: PgfiInitPhase1/2 + PgrInit (as
// rpgen_open_info() above) plus the extra buffers plink2::PgrGet()/PgrGetD()
// themselves need - a sample_include bitvec (and its interleaved twin), its
// cumulative popcounts, a genovec scratch buffer, and (when with_dosage) a
// dosage_present bitvec + dosage_main array. Modeled on pgenlibr's
// RPgenReader::Load(), pared down to only what PgrGet()/PgrGetD() consume:
// no raregeno/difflist buffers (sparse reads), no multiallelic patch01/10
// buffers or phasepresent/phaseinfo (ReadAlleles-style allele-specific or
// phased reads) - rpgen_read_hardcalls()/rpgen_read_dosages() need none of
// those.
//
// sample_include is always filled to mark every sample present (there is no
// sample-subsetting API yet), via the same SetBit()/FillInterleavedMaskVec()/
// FillCumulativePopcounts() sequence pgenlibr's SetSampleSubsetInternal()
// uses for an explicit sample_subset - deliberately not the "leave these
// buffers uninitialized, PgrGet() special-cases sample_ct == raw_sample_ct"
// path pgenlibr's Load() takes when it is called with no sample_subset at
// all: that path is real (Load() does rely on it) but this file has no need
// to depend on undocumented pgenlib-internal behavior when the well-exercised
// explicit-subset path is one small loop away and provably correct.
struct RpgenFullReader {
  plink2::PgenFileInfo info;
  plink2::PgenReader state;
  plink2::RefcountedWptr *allele_idx_offsetsp;
  unsigned char *pgfi_alloc;
  unsigned char *pgr_alloc;

  uintptr_t *subset_include_vec;
  uintptr_t *subset_include_interleaved_vec;
  uint32_t *subset_cumulative_popcounts;
  plink2::PgrSampleSubsetIndex subset_index;
  uintptr_t *genovec;
  uintptr_t *dosage_present;  // nullptr unless opened with_dosage
  uint16_t *dosage_main;      // nullptr unless opened with_dosage

  uint32_t n_sample;
  uint32_t n_variant;
};

void rpgen_full_reader_preinit(RpgenFullReader *r) {
  plink2::PreinitPgfi(&r->info);
  plink2::PreinitPgr(&r->state);
  plink2::PgrSetFreadBuf(nullptr, &r->state);
  r->allele_idx_offsetsp = nullptr;
  r->pgfi_alloc = nullptr;
  r->pgr_alloc = nullptr;
  r->subset_include_vec = nullptr;
  r->subset_include_interleaved_vec = nullptr;
  r->subset_cumulative_popcounts = nullptr;
  r->genovec = nullptr;
  r->dosage_present = nullptr;
  r->dosage_main = nullptr;
  r->n_sample = 0;
  r->n_variant = 0;
}

// Releases every resource rpgen_full_reader_open() may have acquired, safe to
// call on a reader that failed to open partway through (every field it
// touches is nullptr-checked or was preinitialized to a safe value by
// rpgen_full_reader_preinit()). Same aliasing reasoning and cleanup order as
// rpgen_open_info()'s `cleanup` label above; see that function's comment.
void rpgen_full_reader_release(RpgenFullReader *r) {
  plink2::PglErr cleanup_reterr = plink2::kPglRetSuccess;
  plink2::CleanupPgr(&r->state, &cleanup_reterr);
  if (plink2::PgrGetFreadBuf(&r->state)) {
    plink2::aligned_free(plink2::PgrGetFreadBuf(&r->state));
  } else if (r->pgr_alloc) {
    plink2::aligned_free(r->pgr_alloc);
  }

  cleanup_reterr = plink2::kPglRetSuccess;
  plink2::CleanupPgfi(&r->info, &cleanup_reterr);
  if (r->info.vrtypes) {
    plink2::aligned_free(r->info.vrtypes);
  } else if (r->pgfi_alloc) {
    plink2::aligned_free(r->pgfi_alloc);
  }
  plink2::CondReleaseRefcountedWptr(&r->allele_idx_offsetsp);
}

// Opens fname for reading, with `with_dosage` selecting whether the
// dosage_present/dosage_main buffers PgrGetD() needs are also carved out (the
// hardcalls-only reader has no use for them). On success, r->n_sample/
// r->n_variant and all buffer pointers are valid until
// rpgen_full_reader_release(r). On failure, r has already been released (via
// rpgen_full_reader_release()) and every pointer in it is back to nullptr;
// the caller must not release it again.
int rpgen_full_reader_open(const char *fname, bool with_dosage,
                            RpgenFullReader *r, char *errbuf,
                            size_t errbuf_len) {
  if (errbuf_len > 0) {
    errbuf[0] = '\0';
  }
  rpgen_full_reader_preinit(r);

  int rc = -1;
  const uint32_t cur_sample_ct = UINT32_MAX;
  const uint32_t cur_variant_ct = UINT32_MAX;
  plink2::PgenHeaderCtrl header_ctrl;
  uintptr_t pgfi_alloc_cacheline_ct;
  char pgenlib_errbuf[plink2::kPglErrstrBufBlen];

  if (plink2::PgfiInitPhase1(fname, nullptr, cur_variant_ct, cur_sample_ct,
                              &header_ctrl, &r->info, &pgfi_alloc_cacheline_ct,
                              pgenlib_errbuf) != plink2::kPglRetSuccess) {
    snprintf(errbuf, errbuf_len, "%s", &pgenlib_errbuf[7]);
    goto cleanup;
  }

  {
    const uint32_t raw_variant_ct = r->info.raw_variant_ct;

    // Same per-variant-allele-count bookkeeping as rpgen_open_info() (see its
    // comment): required for PgfiInitPhase2() to succeed on a file whose
    // header advertises per-variant allele counts, irrespective of whether a
    // .pvar was supplied (it wasn't - this reader doesn't take one; see
    // rpgen_read_hardcalls()'s comment for why that's fine for a plain
    // PgrGet()/PgrGetD() read).
    if (header_ctrl & 0x30) {
      r->allele_idx_offsetsp = plink2::CreateRefcountedWptr(raw_variant_ct + 1);
      if (!r->allele_idx_offsetsp) {
        snprintf(errbuf, errbuf_len, "out of memory (allele_idx_offsets)");
        goto cleanup;
      }
      r->info.allele_idx_offsets = r->allele_idx_offsetsp->p;
    } else {
      r->info.max_allele_ct = 2;
    }

    if (plink2::cachealigned_malloc(
            pgfi_alloc_cacheline_ct * plink2::kCacheline, &r->pgfi_alloc)) {
      snprintf(errbuf, errbuf_len, "out of memory (pgfi_alloc)");
      goto cleanup;
    }

    uint32_t max_vrec_width;
    uintptr_t pgr_alloc_cacheline_ct;
    if (plink2::PgfiInitPhase2(header_ctrl, 1, 0, 0, 0, raw_variant_ct,
                                &max_vrec_width, &r->info, r->pgfi_alloc,
                                &pgr_alloc_cacheline_ct, pgenlib_errbuf)) {
      snprintf(errbuf, errbuf_len, "%s", &pgenlib_errbuf[7]);
      goto cleanup;
    }

    // pgr_alloc holds two regions back to back: PgrInit()'s own internal
    // working set (pgr_alloc_main_byte_ct, opaque to us) followed by our own
    // sample-subset/genovec/dosage buffers, carved out below exactly the way
    // pgenlibr's Load() carves the same buffers out of its own larger
    // pgr_alloc (it additionally reserves raregeno/difflist/multiallelic-
    // patch/phase/transpose regions we don't need here).
    const uint32_t file_sample_ct = r->info.raw_sample_ct;
    const uintptr_t pgr_alloc_main_byte_ct =
        pgr_alloc_cacheline_ct * plink2::kCacheline;
    const uintptr_t sample_subset_byte_ct =
        plink2::DivUp(file_sample_ct, plink2::kBitsPerVec) *
        plink2::kBytesPerVec;
    const uintptr_t cumulative_popcounts_byte_ct =
        plink2::DivUp(file_sample_ct,
                       plink2::kBitsPerWord * plink2::kInt32PerVec) *
        plink2::kBytesPerVec;
    const uintptr_t genovec_byte_ct =
        plink2::DivUp(file_sample_ct, plink2::kNypsPerVec) *
        plink2::kBytesPerVec;
    const uintptr_t dosage_present_byte_ct =
        with_dosage ? sample_subset_byte_ct : 0;
    const uintptr_t dosage_main_byte_ct =
        with_dosage ? plink2::DivUp(file_sample_ct,
                                     2 * plink2::kInt32PerVec) *
                           plink2::kBytesPerVec
                    : 0;
    const uintptr_t extra_byte_ct =
        2 * sample_subset_byte_ct + cumulative_popcounts_byte_ct +
        genovec_byte_ct + dosage_present_byte_ct + dosage_main_byte_ct;

    if (plink2::cachealigned_malloc(pgr_alloc_main_byte_ct + extra_byte_ct,
                                     &r->pgr_alloc)) {
      snprintf(errbuf, errbuf_len, "out of memory (pgr_alloc)");
      goto cleanup;
    }

    const plink2::PglErr reterr =
        plink2::PgrInit(fname, max_vrec_width, &r->info, &r->state,
                         r->pgr_alloc);
    if (reterr != plink2::kPglRetSuccess) {
      snprintf(errbuf, errbuf_len, "PgrInit() error %d",
               static_cast<int>(reterr));
      goto cleanup;
    }

    unsigned char *iter = &r->pgr_alloc[pgr_alloc_main_byte_ct];
    r->subset_include_vec = reinterpret_cast<uintptr_t *>(iter);
    iter += sample_subset_byte_ct;
    r->subset_include_interleaved_vec = reinterpret_cast<uintptr_t *>(iter);
    iter += sample_subset_byte_ct;
#ifdef USE_AVX2
    r->subset_include_interleaved_vec[-3] = 0;
    r->subset_include_interleaved_vec[-2] = 0;
#endif
    r->subset_include_interleaved_vec[-1] = 0;
    r->subset_cumulative_popcounts = reinterpret_cast<uint32_t *>(iter);
    iter += cumulative_popcounts_byte_ct;
    r->genovec = reinterpret_cast<uintptr_t *>(iter);
    iter += genovec_byte_ct;
    if (with_dosage) {
      r->dosage_present = reinterpret_cast<uintptr_t *>(iter);
      iter += dosage_present_byte_ct;
      r->dosage_main = reinterpret_cast<uint16_t *>(iter);
      iter += dosage_main_byte_ct;
    }

    // Mark every sample included (see the RpgenFullReader comment above for
    // why this always takes the explicit-subset path).
    const uint32_t raw_sample_ctv =
        plink2::DivUp(file_sample_ct, plink2::kBitsPerVec);
    const uint32_t raw_sample_ctaw = raw_sample_ctv * plink2::kWordsPerVec;
    plink2::ZeroWArr(raw_sample_ctaw, r->subset_include_vec);
    for (uint32_t sample_idx = 0; sample_idx != file_sample_ct;
         ++sample_idx) {
      plink2::SetBit(sample_idx, r->subset_include_vec);
    }
    plink2::FillInterleavedMaskVec(r->subset_include_vec, raw_sample_ctv,
                                    r->subset_include_interleaved_vec);
    const uint32_t raw_sample_ctl =
        plink2::DivUp(file_sample_ct, plink2::kBitsPerWord);
    plink2::FillCumulativePopcounts(r->subset_include_vec, raw_sample_ctl,
                                     r->subset_cumulative_popcounts);
    plink2::PgrSetSampleSubsetIndex(r->subset_cumulative_popcounts, &r->state,
                                     &r->subset_index);

    r->n_sample = file_sample_ct;
    r->n_variant = raw_variant_ct;
    rc = 0;
  }

cleanup:
  if (rc != 0) {
    rpgen_full_reader_release(r);
  }
  return rc;
}

// Reads variants [variant_start, variant_start + variant_ct) into `out`, a
// caller-allocated int32_t[reader.n_sample * variant_ct] buffer in
// column-major (variant-major) order - out[v * n_sample + s] is sample s's
// hardcall dosage (0/1/2/NA_INTEGER) at variant variant_start + v. Matches R
// matrix layout directly: a caller allocating an INTSXP matrix of dim
// (n_sample, variant_ct) can pass INTEGER(result) straight through.
int rpgen_read_hardcalls_range(const char *fname, uint32_t variant_start,
                                uint32_t variant_ct, int32_t *out,
                                uint32_t *n_sample_out,
                                uint32_t *n_variant_out, char *errbuf,
                                size_t errbuf_len) {
  RpgenFullReader r;
  if (rpgen_full_reader_open(fname, /*with_dosage=*/false, &r, errbuf,
                              errbuf_len) != 0) {
    return -1;
  }

  int rc = 0;
  if (static_cast<uint64_t>(variant_start) + variant_ct > r.n_variant) {
    snprintf(errbuf, errbuf_len,
             "variant range [%u, %u) out of bounds (raw_variant_ct = %u)",
             variant_start, variant_start + variant_ct, r.n_variant);
    rc = -1;
  }

  // Plain PgrGet() (as opposed to the allele-specific PgrGet1()) returns
  // genovec coded {hom-ref, has-one-non-ref-allele, has-two-non-ref-alleles,
  // missing} even for multiallelic variants - the same collapsed-ALT
  // encoding pgenlibr's own ReadIntList()/ReadList() produce by calling this
  // exact function (see src/pgenlibr.cpp), which is why they don't need a
  // .pvar's allele bookkeeping either: allele identity (which ALT is which)
  // only matters for allele-specific reads (PgrGet1()/PgrGet1D(),
  // ReadAlleles()), not for this collapsed count. No .pvar dependency here.
  for (uint32_t i = 0; rc == 0 && i != variant_ct; ++i) {
    const uint32_t vidx = variant_start + i;
    const plink2::PglErr reterr =
        plink2::PgrGet(r.subset_include_vec, r.subset_index, r.n_sample,
                       vidx, &r.state, r.genovec);
    if (reterr != plink2::kPglRetSuccess) {
      snprintf(errbuf, errbuf_len, "PgrGet() error %d at variant %u",
               static_cast<int>(reterr), vidx);
      rc = -1;
      break;
    }
    plink2::GenoarrLookup256x4bx4(
        r.genovec, kGenoRInt32Quads, r.n_sample,
        &out[static_cast<size_t>(i) * r.n_sample]);
  }

  if (rc == 0) {
    *n_sample_out = r.n_sample;
    *n_variant_out = r.n_variant;
  }
  rpgen_full_reader_release(&r);
  return rc;
}

// Reads variants [variant_start, variant_start + variant_ct) into `out`, a
// caller-allocated double[reader.n_sample * variant_ct] buffer, same
// column-major layout as rpgen_read_hardcalls_range() - out[v * n_sample + s]
// is sample s's dosage in [0, 2] (or NA_REAL) at variant variant_start + v.
// PgrGetD() falls back to the hardcall for samples with no explicit dosage
// record, so every sample gets a value (matching pgenlibr's Read()/
// ReadList(meanimpute = FALSE)).
int rpgen_read_dosages_range(const char *fname, uint32_t variant_start,
                              uint32_t variant_ct, double *out,
                              uint32_t *n_sample_out, uint32_t *n_variant_out,
                              char *errbuf, size_t errbuf_len) {
  RpgenFullReader r;
  if (rpgen_full_reader_open(fname, /*with_dosage=*/true, &r, errbuf,
                              errbuf_len) != 0) {
    return -1;
  }

  int rc = 0;
  if (static_cast<uint64_t>(variant_start) + variant_ct > r.n_variant) {
    snprintf(errbuf, errbuf_len,
             "variant range [%u, %u) out of bounds (raw_variant_ct = %u)",
             variant_start, variant_start + variant_ct, r.n_variant);
    rc = -1;
  }

  for (uint32_t i = 0; rc == 0 && i != variant_ct; ++i) {
    const uint32_t vidx = variant_start + i;
    uint32_t dosage_ct;
    const plink2::PglErr reterr = plink2::PgrGetD(
        r.subset_include_vec, r.subset_index, r.n_sample, vidx, &r.state,
        r.genovec, r.dosage_present, r.dosage_main, &dosage_ct);
    if (reterr != plink2::kPglRetSuccess) {
      snprintf(errbuf, errbuf_len, "PgrGetD() error %d at variant %u",
               static_cast<int>(reterr), vidx);
      rc = -1;
      break;
    }
    plink2::Dosage16ToDoubles(kGenoRDoublePairs, r.genovec, r.dosage_present,
                               r.dosage_main, r.n_sample, dosage_ct,
                               &out[static_cast<size_t>(i) * r.n_sample]);
  }

  if (rc == 0) {
    *n_sample_out = r.n_sample;
    *n_variant_out = r.n_variant;
  }
  rpgen_full_reader_release(&r);
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

// Reads variants [variant_start, variant_start + variant_ct) for every
// sample into `out` (caller-allocated, n_sample * variant_ct int32_t values,
// column-major - see rpgen_read_hardcalls_range()'s comment). n_sample_out is
// always the file's full raw_sample_ct (there is no sample-subsetting API
// yet); n_variant_out is the file's full raw_variant_ct, independent of
// variant_ct, so callers can tell a short read (variant_ct <
// raw_variant_ct) from an out-of-range one.
int Rpgen_read_hardcalls(const char *path, uint32_t variant_start,
                          uint32_t variant_ct, int32_t *out,
                          uint32_t *n_sample_out, uint32_t *n_variant_out,
                          char *errbuf, size_t errbuf_len) {
  return rpgen_read_hardcalls_range(path, variant_start, variant_ct, out,
                                     n_sample_out, n_variant_out, errbuf,
                                     errbuf_len);
}

// Same contract as Rpgen_read_hardcalls(), but `out` holds double dosages in
// [0, 2] (or NA_REAL) - see rpgen_read_dosages_range()'s comment.
int Rpgen_read_dosages(const char *path, uint32_t variant_start,
                        uint32_t variant_ct, double *out,
                        uint32_t *n_sample_out, uint32_t *n_variant_out,
                        char *errbuf, size_t errbuf_len) {
  return rpgen_read_dosages_range(path, variant_start, variant_ct, out,
                                   n_sample_out, n_variant_out, errbuf,
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

// Shared argument validation for RC_rpgen_read_hardcalls()/
// RC_rpgen_read_dosages(): `path` must be a single non-NA string; `pvar` must
// be NULL or a single string. `pvar` is accepted for API parity with the
// milestone 2 spec (and a future allele-specific reader) but not read yet -
// see rpgen_read_hardcalls_range()'s comment on why a plain PgrGet()/
// PgrGetD() collapsed-ALT read does not need a .pvar's allele bookkeeping.
static const char *rpgen_check_path_and_pvar(SEXP path_sexp, SEXP pvar_sexp) {
  if (TYPEOF(path_sexp) != STRSXP || Rf_length(path_sexp) != 1 ||
      STRING_ELT(path_sexp, 0) == NA_STRING) {
    Rf_error("path must be a single non-NA string");
  }
  if (pvar_sexp != R_NilValue &&
      !(TYPEOF(pvar_sexp) == STRSXP && Rf_length(pvar_sexp) == 1 &&
        STRING_ELT(pvar_sexp, 0) != NA_STRING)) {
    Rf_error("pvar must be NULL or a single non-NA string");
  }
  return CHAR(STRING_ELT(path_sexp, 0));
}

SEXP RC_rpgen_read_hardcalls(SEXP path_sexp, SEXP pvar_sexp) {
  const char *path = rpgen_check_path_and_pvar(path_sexp, pvar_sexp);

  uint32_t n_sample = 0;
  uint32_t n_variant = 0;
  char errbuf[512];
  if (Rpgen_open_info(path, &n_sample, &n_variant, errbuf, sizeof(errbuf)) !=
      0) {
    Rf_error("Rpgen_open_info(\"%s\") failed: %s", path, errbuf);
  }
  if (n_sample > static_cast<uint32_t>(INT_MAX) ||
      n_variant > static_cast<uint32_t>(INT_MAX)) {
    Rf_error("\"%s\" is too large for an R matrix (%u x %u)", path, n_sample,
             n_variant);
  }

  SEXP result = PROTECT(Rf_allocMatrix(
      INTSXP, static_cast<int>(n_sample), static_cast<int>(n_variant)));
  uint32_t n_sample2 = 0;
  uint32_t n_variant2 = 0;
  if (Rpgen_read_hardcalls(path, 0, n_variant, INTEGER(result), &n_sample2,
                            &n_variant2, errbuf, sizeof(errbuf)) != 0) {
    UNPROTECT(1);
    Rf_error("Rpgen_read_hardcalls(\"%s\") failed: %s", path, errbuf);
  }
  if (n_sample2 != n_sample || n_variant2 != n_variant) {
    UNPROTECT(1);
    Rf_error("Rpgen_read_hardcalls(\"%s\"): file changed between open and "
             "read (%u x %u -> %u x %u)",
             path, n_sample, n_variant, n_sample2, n_variant2);
  }
  UNPROTECT(1);
  return result;
}

SEXP RC_rpgen_read_dosages(SEXP path_sexp, SEXP pvar_sexp) {
  const char *path = rpgen_check_path_and_pvar(path_sexp, pvar_sexp);

  uint32_t n_sample = 0;
  uint32_t n_variant = 0;
  char errbuf[512];
  if (Rpgen_open_info(path, &n_sample, &n_variant, errbuf, sizeof(errbuf)) !=
      0) {
    Rf_error("Rpgen_open_info(\"%s\") failed: %s", path, errbuf);
  }
  if (n_sample > static_cast<uint32_t>(INT_MAX) ||
      n_variant > static_cast<uint32_t>(INT_MAX)) {
    Rf_error("\"%s\" is too large for an R matrix (%u x %u)", path, n_sample,
             n_variant);
  }

  SEXP result = PROTECT(Rf_allocMatrix(
      REALSXP, static_cast<int>(n_sample), static_cast<int>(n_variant)));
  uint32_t n_sample2 = 0;
  uint32_t n_variant2 = 0;
  if (Rpgen_read_dosages(path, 0, n_variant, REAL(result), &n_sample2,
                          &n_variant2, errbuf, sizeof(errbuf)) != 0) {
    UNPROTECT(1);
    Rf_error("Rpgen_read_dosages(\"%s\") failed: %s", path, errbuf);
  }
  if (n_sample2 != n_sample || n_variant2 != n_variant) {
    UNPROTECT(1);
    Rf_error("Rpgen_read_dosages(\"%s\"): file changed between open and "
             "read (%u x %u -> %u x %u)",
             path, n_sample, n_variant, n_sample2, n_variant2);
  }
  UNPROTECT(1);
  return result;
}

static const R_CallMethodDef CallEntries[] = {
    {"RC_rpgen_info", (DL_FUNC)&RC_rpgen_info, 1},
    {"RC_rpgen_read_hardcalls", (DL_FUNC)&RC_rpgen_read_hardcalls, 2},
    {"RC_rpgen_read_dosages", (DL_FUNC)&RC_rpgen_read_dosages, 2},
    {NULL, NULL, 0}};

static void register_c_callables(DllInfo *dll) {
  R_RegisterCCallable("Rpgen", "Rpgen_api_version",
                       (DL_FUNC)Rpgen_api_version);
  R_RegisterCCallable("Rpgen", "Rpgen_open_info", (DL_FUNC)Rpgen_open_info);
  R_RegisterCCallable("Rpgen", "Rpgen_read_hardcalls",
                       (DL_FUNC)Rpgen_read_hardcalls);
  R_RegisterCCallable("Rpgen", "Rpgen_read_dosages",
                       (DL_FUNC)Rpgen_read_dosages);
  (void)dll;
}

void R_init_Rpgen(DllInfo *dll) {
  R_registerRoutines(dll, NULL, CallEntries, NULL, NULL);
  register_c_callables(dll);
  R_useDynamicSymbols(dll, FALSE);
}

}  // extern "C"
