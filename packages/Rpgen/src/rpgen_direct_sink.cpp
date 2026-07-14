/*
 * Direct PLINK 2 importer to Rfmalloc record transfer.
 *
 * The vendored importers already produce the semantic record we need before
 * passing it to STPgenWriter.  During rpgen_ingest(), a small generated-tree
 * patch redirects that terminal append here.  Ordinary rpgen_import_*()
 * calls leave this hook inactive and still write standard PGEN files.
 */

#include <climits>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>

#include <R.h>
#include <Rinternals.h>
#include <R_ext/Utils.h>

#include <Rfmalloc.h>

#include "rpgen_direct_sink.h"

namespace {

enum rpgen_direct_kind {
  RPGEN_DIRECT_HARDCALL = 0,
  RPGEN_DIRECT_DOSAGE = 1,
  RPGEN_DIRECT_HAPLOTYPE = 2,
  RPGEN_DIRECT_F64 = 3
};

struct rpgen_direct_state {
  int active;
  int opened;
  int writer_finished;
  int kind;
  SEXP runtime;
  Rfmalloc_buffer_context *buffer;
  Rfmalloc_buffer_open_fun buffer_open;
  Rfmalloc_buffer_write_fun buffer_write;
  Rfmalloc_buffer_finish_fun buffer_finish;
  Rfmalloc_buffer_abort_fun buffer_abort;
  void *row;
  size_t row_bytes;
  uintptr_t *genovec_scratch;
  size_t genovec_word_ct;
  uint32_t sample_ct;
  uint32_t variant_ct;
  uint32_t writer_variant_limit;
  uint32_t variant_ct_hint;
  char error[256];
};

static rpgen_direct_state direct_state;

static void
rpgen_direct_error(const char *message)
{
  if (!direct_state.error[0]) {
    snprintf(direct_state.error, sizeof(direct_state.error), "%s", message);
  }
}

static void
rpgen_direct_release(int abort_buffer)
{
  if (direct_state.buffer) {
    if (abort_buffer) {
      direct_state.buffer_abort(direct_state.buffer);
    }
    direct_state.buffer = nullptr;
  }
  free(direct_state.row);
  direct_state.row = nullptr;
  free(direct_state.genovec_scratch);
  direct_state.genovec_scratch = nullptr;
  if (direct_state.runtime && direct_state.runtime != R_NilValue) {
    R_ReleaseObject(direct_state.runtime);
  }
  memset(&direct_state, 0, sizeof(direct_state));
}

struct rpgen_direct_open_call {
  const char *storage;
  R_xlen_t n_item;
  R_xlen_t n_record;
  Rfmalloc_buffer_context *result;
};

static void
rpgen_direct_open_exec(void *raw)
{
  rpgen_direct_open_call *call =
      static_cast<rpgen_direct_open_call *>(raw);
  call->result = direct_state.buffer_open(direct_state.runtime, call->storage,
      call->n_item, call->n_record);
}

static inline uint32_t
rpgen_direct_geno(const uintptr_t *genovec, uint32_t sample_idx)
{
  const uint32_t nyps_per_word = sizeof(uintptr_t) * CHAR_BIT / 2;
  return static_cast<uint32_t>(
      (genovec[sample_idx / nyps_per_word] >>
       (2 * (sample_idx % nyps_per_word))) & 3);
}

static inline int
rpgen_direct_bit(const uintptr_t *bits, uint32_t bit_idx)
{
  const uint32_t bits_per_word = sizeof(uintptr_t) * CHAR_BIT;
  return bits &&
      ((bits[bit_idx / bits_per_word] >> (bit_idx % bits_per_word)) & 1);
}

static int
rpgen_direct_fill_hardcalls(const uintptr_t *genovec)
{
  int32_t *out = static_cast<int32_t *>(direct_state.row);
  for (uint32_t sample_idx = 0; sample_idx != direct_state.sample_ct;
       ++sample_idx) {
    const uint32_t geno = rpgen_direct_geno(genovec, sample_idx);
    out[sample_idx] = geno == 3 ? NA_INTEGER : static_cast<int32_t>(geno);
  }
  return 0;
}

static int
rpgen_direct_fill_dosages(const uintptr_t *genovec,
    const uintptr_t *dosage_present, const uint16_t *dosage_main,
    uint32_t dosage_ct)
{
  if (dosage_ct && (!dosage_present || !dosage_main)) {
    rpgen_direct_error("PLINK 2 supplied an incomplete dosage track");
    return -1;
  }

  double *out = static_cast<double *>(direct_state.row);
  uint32_t dosage_idx = 0;
  for (uint32_t sample_idx = 0; sample_idx != direct_state.sample_ct;
       ++sample_idx) {
    if (rpgen_direct_bit(dosage_present, sample_idx)) {
      if (dosage_idx == dosage_ct) {
        rpgen_direct_error("PLINK 2 dosage bitmap exceeds its value array");
        return -1;
      }
      const uint16_t dosage = dosage_main[dosage_idx++];
      if (dosage == UINT16_MAX) {
        out[sample_idx] = NA_REAL;
      } else if (dosage > 32768) {
        rpgen_direct_error("PLINK 2 supplied a dosage outside [0, 2]");
        return -1;
      } else {
        out[sample_idx] = static_cast<double>(dosage) / 16384.0;
      }
    } else {
      const uint32_t geno = rpgen_direct_geno(genovec, sample_idx);
      out[sample_idx] = geno == 3 ? NA_REAL : static_cast<double>(geno);
    }
  }
  if (dosage_idx != dosage_ct) {
    rpgen_direct_error("PLINK 2 dosage value array exceeds its bitmap");
    return -1;
  }
  return 0;
}

static int
rpgen_direct_fill_haplotypes(const uintptr_t *genovec, int phase_supplied,
    const uintptr_t *phasepresent, const uintptr_t *phaseinfo,
    uint32_t variant_idx)
{
  uint8_t *out = static_cast<uint8_t *>(direct_state.row);
  memset(out, 0, direct_state.row_bytes);
  for (uint32_t sample_idx = 0; sample_idx != direct_state.sample_ct;
       ++sample_idx) {
    const uint32_t geno = rpgen_direct_geno(genovec, sample_idx);
    const size_t hap0 = static_cast<size_t>(sample_idx) * 2;
    if (geno == 3) {
      snprintf(direct_state.error, sizeof(direct_state.error),
          "missing genotype at variant %u, sample %u", variant_idx,
          sample_idx);
      return -1;
    }
    if (geno == 2) {
      out[hap0 >> 3] |= static_cast<uint8_t>(1u << (hap0 & 7));
      out[(hap0 + 1) >> 3] |=
          static_cast<uint8_t>(1u << ((hap0 + 1) & 7));
      continue;
    }
    if (geno != 1) {
      continue;
    }
    if (!phase_supplied || (phasepresent &&
                            !rpgen_direct_bit(phasepresent, sample_idx))) {
      snprintf(direct_state.error, sizeof(direct_state.error),
          "unphased heterozygote at variant %u, sample %u", variant_idx,
          sample_idx);
      return -1;
    }
    if (!phaseinfo) {
      rpgen_direct_error("PLINK 2 supplied phase presence without phase data");
      return -1;
    }
    const size_t alt_hap = hap0 +
        (rpgen_direct_bit(phaseinfo, sample_idx) ? 0 : 1);
    out[alt_hap >> 3] |= static_cast<uint8_t>(1u << (alt_hap & 7));
  }
  return 0;
}

} // namespace

extern "C" int
rpgen_direct_sink_active(void)
{
  return direct_state.active;
}

extern "C" const char *
rpgen_direct_sink_error(void)
{
  return direct_state.error;
}

extern "C" int
rpgen_direct_sink_open(uint32_t variant_ct, uint32_t sample_ct)
{
  if (!direct_state.active || direct_state.opened || !variant_ct ||
      !sample_ct) {
    rpgen_direct_error("invalid or repeated direct genotype sink open");
    return -1;
  }

  const uint32_t destination_variant_ct = direct_state.variant_ct_hint ?
      direct_state.variant_ct_hint : variant_ct;
  if (!destination_variant_ct || destination_variant_ct > variant_ct) {
    rpgen_direct_error("direct genotype variant count exceeds writer limit");
    return -1;
  }

  const char *storage;
  R_xlen_t n_item;
  size_t row_bytes;
  switch (direct_state.kind) {
  case RPGEN_DIRECT_HARDCALL:
    storage = "bed";
    n_item = static_cast<R_xlen_t>(sample_ct);
    if (sample_ct > SIZE_MAX / sizeof(int32_t)) {
      rpgen_direct_error("hardcall record is too large");
      return -1;
    }
    row_bytes = static_cast<size_t>(sample_ct) * sizeof(int32_t);
    break;
  case RPGEN_DIRECT_DOSAGE:
  case RPGEN_DIRECT_F64:
    storage = direct_state.kind == RPGEN_DIRECT_DOSAGE ? "dosage" : "f64";
    n_item = static_cast<R_xlen_t>(sample_ct);
    if (sample_ct > SIZE_MAX / sizeof(double)) {
      rpgen_direct_error("dosage record is too large");
      return -1;
    }
    row_bytes = static_cast<size_t>(sample_ct) * sizeof(double);
    break;
  case RPGEN_DIRECT_HAPLOTYPE:
    storage = "haplotype";
    if (static_cast<uint64_t>(sample_ct) * 2 >
            static_cast<uint64_t>(std::numeric_limits<R_xlen_t>::max()) ||
        static_cast<uint64_t>(sample_ct) * 2 >
            static_cast<uint64_t>(SIZE_MAX) - 7) {
      rpgen_direct_error("haplotype record is too large");
      return -1;
    }
    n_item = static_cast<R_xlen_t>(sample_ct) * 2;
    row_bytes = (static_cast<size_t>(sample_ct) * 2 + 7) / 8;
    break;
  default:
    rpgen_direct_error("unknown direct genotype destination");
    return -1;
  }

  direct_state.row = malloc(row_bytes);
  if (!direct_state.row) {
    rpgen_direct_error("failed to allocate one direct genotype record");
    return -1;
  }
  direct_state.row_bytes = row_bytes;
  const size_t nyps_per_word = sizeof(uintptr_t) * CHAR_BIT / 2;
  const uint64_t genovec_word_ct =
      (static_cast<uint64_t>(sample_ct) + nyps_per_word - 1) /
      nyps_per_word;
  if (genovec_word_ct > SIZE_MAX) {
    free(direct_state.row);
    direct_state.row = nullptr;
    direct_state.row_bytes = 0;
    rpgen_direct_error("genotype record is too large");
    return -1;
  }
  direct_state.genovec_word_ct = static_cast<size_t>(genovec_word_ct);
  if (direct_state.genovec_word_ct > SIZE_MAX / sizeof(uintptr_t)) {
    free(direct_state.row);
    direct_state.row = nullptr;
    direct_state.row_bytes = 0;
    rpgen_direct_error("genotype record is too large");
    return -1;
  }
  direct_state.genovec_scratch = static_cast<uintptr_t *>(
      calloc(direct_state.genovec_word_ct, sizeof(uintptr_t)));
  if (!direct_state.genovec_scratch) {
    free(direct_state.row);
    direct_state.row = nullptr;
    direct_state.row_bytes = 0;
    rpgen_direct_error("failed to allocate one packed genotype record");
    return -1;
  }

  rpgen_direct_open_call call = {
      storage, n_item, static_cast<R_xlen_t>(destination_variant_ct), nullptr};
  if (R_ToplevelExec(rpgen_direct_open_exec, &call) == FALSE ||
      !call.result) {
    free(direct_state.row);
    direct_state.row = nullptr;
    direct_state.row_bytes = 0;
    free(direct_state.genovec_scratch);
    direct_state.genovec_scratch = nullptr;
    direct_state.genovec_word_ct = 0;
    rpgen_direct_error("Rfmalloc rejected the direct genotype destination");
    return -1;
  }

  direct_state.buffer = call.result;
  direct_state.sample_ct = sample_ct;
  direct_state.variant_ct = destination_variant_ct;
  direct_state.writer_variant_limit = variant_ct;
  direct_state.opened = 1;
  return 0;
}

extern "C" int
rpgen_direct_sink_append_difflist(uint32_t variant_idx, uint32_t sample_ct,
    const uintptr_t *raregeno, const uint32_t *difflist_sample_ids,
    uint32_t difflist_common_geno, uint32_t difflist_len)
{
  if (!direct_state.active || !direct_state.opened || !raregeno ||
      !difflist_sample_ids || difflist_common_geno > 3 ||
      sample_ct != direct_state.sample_ct ||
      !direct_state.genovec_scratch) {
    rpgen_direct_error("invalid direct genotype difflist");
    return -1;
  }

  memset(direct_state.genovec_scratch, 0,
      direct_state.genovec_word_ct * sizeof(uintptr_t));
  const uint32_t nyps_per_word = sizeof(uintptr_t) * CHAR_BIT / 2;
  for (uint32_t sample_idx = 0; sample_idx != sample_ct; ++sample_idx) {
    direct_state.genovec_scratch[sample_idx / nyps_per_word] |=
        static_cast<uintptr_t>(difflist_common_geno) <<
        (2 * (sample_idx % nyps_per_word));
  }
  uint32_t previous_sample_idx = 0;
  for (uint32_t rare_idx = 0; rare_idx != difflist_len; ++rare_idx) {
    const uint32_t sample_idx = difflist_sample_ids[rare_idx];
    if (sample_idx >= sample_ct ||
        (rare_idx && sample_idx <= previous_sample_idx)) {
      rpgen_direct_error("invalid sample index in genotype difflist");
      return -1;
    }
    previous_sample_idx = sample_idx;
    const uint32_t rare_geno = rpgen_direct_geno(raregeno, rare_idx);
    uintptr_t *word =
        &direct_state.genovec_scratch[sample_idx / nyps_per_word];
    const uint32_t shift = 2 * (sample_idx % nyps_per_word);
    *word = (*word & ~(static_cast<uintptr_t>(3) << shift)) |
        (static_cast<uintptr_t>(rare_geno) << shift);
  }
  return rpgen_direct_sink_append(variant_idx, sample_ct,
      direct_state.genovec_scratch, 0, nullptr, nullptr, nullptr, nullptr, 0);
}

extern "C" int
rpgen_direct_sink_append(uint32_t variant_idx, uint32_t sample_ct,
    const uintptr_t *genovec, int phase_supplied,
    const uintptr_t *phasepresent, const uintptr_t *phaseinfo,
    const uintptr_t *dosage_present, const uint16_t *dosage_main,
    uint32_t dosage_ct)
{
  if (!direct_state.active || !direct_state.opened ||
      direct_state.writer_finished || !genovec ||
      sample_ct != direct_state.sample_ct ||
      variant_idx >= direct_state.variant_ct) {
    rpgen_direct_error("invalid direct genotype record");
    return -1;
  }

  int source_type;
  int rc;
  if (direct_state.kind == RPGEN_DIRECT_HARDCALL) {
    source_type = RFMALLOC_BUFFER_I32;
    rc = rpgen_direct_fill_hardcalls(genovec);
  } else if (direct_state.kind == RPGEN_DIRECT_HAPLOTYPE) {
    source_type = RFMALLOC_BUFFER_PACKED_BITS;
    rc = rpgen_direct_fill_haplotypes(genovec, phase_supplied, phasepresent,
        phaseinfo, variant_idx);
  } else {
    source_type = RFMALLOC_BUFFER_F64;
    rc = rpgen_direct_fill_dosages(genovec, dosage_present, dosage_main,
        dosage_ct);
  }
  if (rc != 0) {
    return -1;
  }
  if (direct_state.buffer_write(direct_state.buffer, variant_idx, 1,
      source_type, direct_state.row, direct_state.row_bytes) != 0) {
    rpgen_direct_error("Rfmalloc rejected a direct genotype record");
    return -1;
  }
  return 0;
}

extern "C" int
rpgen_direct_sink_writer_finish(uint32_t variant_ct,
    uint32_t emitted_variant_ct)
{
  if (!direct_state.active || !direct_state.opened ||
      variant_ct != direct_state.writer_variant_limit ||
      emitted_variant_ct != direct_state.variant_ct) {
    rpgen_direct_error("PLINK 2 emitted an incomplete direct genotype stream");
    return -1;
  }
  direct_state.writer_finished = 1;
  return 0;
}

extern "C" SEXP
RC_rpgen_direct_sink_begin(SEXP kind_sexp, SEXP runtime_sexp,
    SEXP variant_ct_hint_sexp)
{
  const int kind = Rf_asInteger(kind_sexp);
  const double variant_ct_hint = Rf_asReal(variant_ct_hint_sexp);
  Rfmalloc_buffer_open_fun buffer_open;
  Rfmalloc_buffer_write_fun buffer_write;
  Rfmalloc_buffer_finish_fun buffer_finish;
  Rfmalloc_buffer_abort_fun buffer_abort;
  if (kind < RPGEN_DIRECT_HARDCALL || kind > RPGEN_DIRECT_F64) {
    Rf_error("invalid direct genotype destination");
  }
  if (direct_state.active) {
    Rf_error("a direct genotype import is already active");
  }
  if (!R_FINITE(variant_ct_hint) || variant_ct_hint < 0 ||
      variant_ct_hint > UINT32_MAX ||
      variant_ct_hint != std::floor(variant_ct_hint)) {
    Rf_error("direct genotype variant count hint is invalid");
  }

  /* Resolve the contract before making the process-global hook visible. */
  buffer_open = Rfmalloc_buffer_open_ptr();
  buffer_write = Rfmalloc_buffer_write_ptr();
  buffer_finish = Rfmalloc_buffer_finish_ptr();
  buffer_abort = Rfmalloc_buffer_abort_ptr();
  R_PreserveObject(runtime_sexp);

  memset(&direct_state, 0, sizeof(direct_state));
  direct_state.active = 1;
  direct_state.kind = kind;
  direct_state.variant_ct_hint = static_cast<uint32_t>(variant_ct_hint);
  direct_state.runtime = runtime_sexp;
  direct_state.buffer_open = buffer_open;
  direct_state.buffer_write = buffer_write;
  direct_state.buffer_finish = buffer_finish;
  direct_state.buffer_abort = buffer_abort;
  return R_NilValue;
}

extern "C" SEXP
RC_rpgen_direct_sink_finish(void)
{
  if (!direct_state.active || !direct_state.opened ||
      !direct_state.writer_finished || !direct_state.buffer) {
    char error_message[sizeof(direct_state.error)];
    snprintf(error_message, sizeof(error_message), "%s",
        direct_state.error[0] ? direct_state.error :
        "direct genotype import did not finish");
    rpgen_direct_release(1);
    Rf_error("%s", error_message);
  }

  const uint32_t sample_ct = direct_state.sample_ct;
  const uint32_t variant_ct = direct_state.variant_ct;
  Rfmalloc_buffer_context *buffer = direct_state.buffer;
  direct_state.buffer = nullptr;

  SEXP ans = PROTECT(Rf_allocVector(VECSXP, 3));
  SEXP payload = PROTECT(direct_state.buffer_finish(buffer));
  if (payload == R_NilValue) {
    UNPROTECT(2);
    rpgen_direct_release(0);
    Rf_error("Rfmalloc failed to finish the direct genotype destination");
  }
  SET_VECTOR_ELT(ans, 0, payload);
  SET_VECTOR_ELT(ans, 1, Rf_ScalarReal(static_cast<double>(sample_ct)));
  SET_VECTOR_ELT(ans, 2, Rf_ScalarReal(static_cast<double>(variant_ct)));
  SEXP names = PROTECT(Rf_allocVector(STRSXP, 3));
  SET_STRING_ELT(names, 0, Rf_mkChar("payload"));
  SET_STRING_ELT(names, 1, Rf_mkChar("n_sample"));
  SET_STRING_ELT(names, 2, Rf_mkChar("n_variant"));
  Rf_setAttrib(ans, R_NamesSymbol, names);

  rpgen_direct_release(0);
  UNPROTECT(3);
  return ans;
}

extern "C" SEXP
RC_rpgen_direct_sink_abort(void)
{
  if (direct_state.active) {
    rpgen_direct_release(1);
  }
  return R_NilValue;
}
