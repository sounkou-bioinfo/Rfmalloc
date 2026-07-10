/*
 * R interface for RfmallocStatgen's native code. Uses R's C API directly (no
 * Rcpp) and includes only pcaone_backend.h - never the PCAone headers - so R's
 * macro remaps and PCAone's C++ never share a translation unit. GPL-3.
 */
#define R_NO_REMAP
#include <R.h>
#include <Rinternals.h>
#include <R_ext/Rdynload.h>
#include <Rfmalloc.h>  // Rfmalloc_tensor_decode (C-callable, API v7)

#include <cstddef>
#include <cstring>
#include <cmath>
#include <exception>
#include <vector>

#include "pcaone_backend.h"

// Build the list(d, u, v) result shared by the in-core and out-of-core paths.
static SEXP build_result(const rfmstatgen::RsvdResult& r) {
  SEXP d = PROTECT(Rf_allocVector(REALSXP, r.k));
  if (r.k > 0) std::memcpy(REAL(d), r.d.data(), sizeof(double) * (size_t)r.k);
  SEXP u = PROTECT(Rf_allocMatrix(REALSXP, r.n, r.k));
  if (r.k > 0) std::memcpy(REAL(u), r.u.data(), sizeof(double) * (size_t)r.n * (size_t)r.k);
  SEXP v = PROTECT(Rf_allocMatrix(REALSXP, r.m, r.k));
  if (r.k > 0) std::memcpy(REAL(v), r.v.data(), sizeof(double) * (size_t)r.m * (size_t)r.k);

  SEXP out = PROTECT(Rf_allocVector(VECSXP, 3));
  SET_VECTOR_ELT(out, 0, d);
  SET_VECTOR_ELT(out, 1, u);
  SET_VECTOR_ELT(out, 2, v);
  SEXP nms = PROTECT(Rf_allocVector(STRSXP, 3));
  SET_STRING_ELT(nms, 0, Rf_mkChar("d"));
  SET_STRING_ELT(nms, 1, Rf_mkChar("u"));
  SET_STRING_ELT(nms, 2, Rf_mkChar("v"));
  Rf_setAttrib(out, R_NamesSymbol, nms);

  UNPROTECT(5);
  return out;
}

extern "C" SEXP C_statgen_pca_incore(SEXP Xs, SEXP ks, SEXP ps, SEXP ss,
                                     SEXP methods, SEXP tols, SEXP seeds) {
  if (!Rf_isMatrix(Xs) || TYPEOF(Xs) != REALSXP) {
    Rf_error("X must be a numeric matrix");
  }
  const int n = Rf_nrows(Xs);
  const int m = Rf_ncols(Xs);
  const int k = Rf_asInteger(ks);
  const int p = Rf_asInteger(ps);
  const int s = Rf_asInteger(ss);
  const int method = Rf_asInteger(methods);
  const double tol = Rf_asReal(tols);
  const int seed = Rf_asInteger(seeds);

  rfmstatgen::RsvdResult r;
  try {
    r = rfmstatgen::rsvd_incore(REAL(Xs), n, m, k, p, s, method, tol, seed);
  } catch (const std::exception& e) {
    Rf_error("statgen_pca: %s", e.what());
  } catch (...) {
    Rf_error("statgen_pca: unknown C++ exception in RSVD backend");
  }
  return build_result(r);
}

// Adapter matching rfmstatgen::decode_range_fn: forward one variant-column
// range of the tensor to Rfmalloc's registered codec decode (C-callable). Kept
// on the R side so the PCAone backend stays free of R headers.
static int glue_decode_tensor(void* tensor, long long elem_offset,
                              long long n_elems, double* out) {
  return Rfmalloc_tensor_decode((SEXP)tensor, (R_xlen_t)elem_offset,
                                (R_xlen_t)n_elems, out);
}

extern "C" SEXP C_statgen_pca_ooc(SEXP tensor, SEXP ns, SEXP ms, SEXP ks,
                                  SEXP ps, SEXP ss, SEXP tols, SEXP seeds,
                                  SEXP block_sizes) {
  const int n = Rf_asInteger(ns);
  const int m = Rf_asInteger(ms);
  const int k = Rf_asInteger(ks);
  const int p = Rf_asInteger(ps);
  const int s = Rf_asInteger(ss);
  const double tol = Rf_asReal(tols);
  const int seed = Rf_asInteger(seeds);
  const int block_size = Rf_asInteger(block_sizes);

  rfmstatgen::RsvdResult r;
  try {
    r = rfmstatgen::rsvd_ooc((void*)tensor, glue_decode_tensor, n, m, k, p, s,
                             tol, seed, block_size);
  } catch (const std::exception& e) {
    Rf_error("statgen_pca (out-of-core): %s", e.what());
  } catch (...) {
    Rf_error("statgen_pca (out-of-core): unknown C++ exception in RSVD backend");
  }
  return build_result(r);
}

// ---------------------------------------------------------------------------
// statgen_snp_cor: stream a genotype tensor one variant-column at a time via
// Rfmalloc_tensor_decode, compute the windowed Pearson correlation between
// nearby variants (mean-impute + centre + unit-L2-normalize each column, then
// r = dot of the two unit columns), and pack the banded result into an
// Rfmalloc "ld" store (Rfmalloc_ld_build). The dense p x p is never formed and
// only a sliding window of unit columns (bounded by the window width, not m) is
// held resident. Storage-agnostic result: the caller reads it with
// ld_col()/ld_pair() or hands it to LDpred2.
// ---------------------------------------------------------------------------
extern "C" SEXP C_statgen_snp_cor(SEXP tensor, SEXP ns, SEXP ms, SEXP sizes,
                                  SEXP thr_r2s, SEXP infos_pos, SEXP bitss) {
  const R_xlen_t n = (R_xlen_t)Rf_asInteger(ns);
  const R_xlen_t m = (R_xlen_t)Rf_asInteger(ms);
  const double thr_r2 = Rf_asReal(thr_r2s);
  const int bits = Rf_asInteger(bitss);
  if (n <= 0 || m <= 0) Rf_error("genotype tensor must be non-empty");
  if (bits != 8 && bits != 16) Rf_error("bits must be 8 or 16");

  const bool has_pos = (infos_pos != R_NilValue);
  const double* pos = has_pos ? REAL(infos_pos) : nullptr;
  // bigsnpr semantics: with infos.pos the window is 'size' kb -> size * 1000 in
  // position units; without it, 'size' is a half-width in number of variants.
  const double W = has_pos ? (Rf_asReal(sizes) * 1000.0) : 0.0;
  const R_xlen_t size_idx = has_pos ? 0 : (R_xlen_t)Rf_asInteger(sizes);
  if (has_pos && (R_xlen_t)Rf_xlength(infos_pos) != m)
    Rf_error("infos_pos must have length equal to the number of variants");

  // Per-column contiguous band [lo_j, hi_j] (0-based), the cumulative offset
  // table, and the total number of stored entries.
  std::vector<R_xlen_t> lo((size_t)m), hi((size_t)m), len((size_t)m);
  std::vector<R_xlen_t> cum((size_t)m + 1);
  {
    R_xlen_t a = 0, b = 0;  // two pointers for the position-based window
    R_xlen_t acc = 0;
    for (R_xlen_t j = 0; j < m; j++) {
      R_xlen_t loj, hij;
      if (has_pos) {
        if (a > j) a = j;
        while (a < j && pos[a] < pos[j] - W) a++;
        if (b < j) b = j;
        while (b + 1 < m && pos[b + 1] <= pos[j] + W) b++;
        loj = a;
        hij = b;
      } else {
        loj = (j > size_idx) ? (j - size_idx) : 0;
        hij = (j + size_idx < m - 1) ? (j + size_idx) : (m - 1);
      }
      lo[(size_t)j] = loj;
      hi[(size_t)j] = hij;
      len[(size_t)j] = hij - loj + 1;
      cum[(size_t)j] = acc;
      acc += len[(size_t)j];
    }
    cum[(size_t)m] = acc;
  }
  const R_xlen_t nnz = cum[(size_t)m];

  std::vector<double> rvals((size_t)nnz, 0.0);
  std::vector<std::vector<double> > cols((size_t)m);  // sliding unit columns
  std::vector<double> buf((size_t)n);
  R_xlen_t cleared = 0;  // columns < cleared have been evicted

  for (R_xlen_t j = 0; j < m; j++) {
    // Evict columns that no future j (>= this one) can neighbour: lo is
    // non-decreasing, so anything below lo[j] is done.
    for (; cleared < lo[(size_t)j]; cleared++) {
      std::vector<double>().swap(cols[(size_t)cleared]);
    }

    if (Rfmalloc_tensor_decode(tensor, j * n, n, buf.data()) != 0) {
      Rf_error("statgen_snp_cor: failed to decode variant column %lld",
               (long long)j);
    }

    // mean over observed, then impute-to-mean + centre + unit-L2 normalize.
    double s = 0.0;
    R_xlen_t cnt = 0;
    for (R_xlen_t i = 0; i < n; i++) {
      const double v = buf[(size_t)i];
      if (!ISNAN(v)) { s += v; cnt++; }
    }
    const double mean = (cnt > 0) ? (s / (double)cnt) : 0.0;
    std::vector<double> u((size_t)n);
    double ss = 0.0;
    for (R_xlen_t i = 0; i < n; i++) {
      const double v = buf[(size_t)i];
      const double c = ISNAN(v) ? 0.0 : (v - mean);  // missing -> mean -> 0
      u[(size_t)i] = c;
      ss += c * c;
    }
    const double norm = std::sqrt(ss);
    if (norm > 0.0) {
      for (R_xlen_t i = 0; i < n; i++) u[(size_t)i] /= norm;
    }  // else monomorphic: unit column stays 0, so all its off-diagonals are 0
    cols[(size_t)j].swap(u);

    // diagonal r = 1
    rvals[(size_t)(cum[(size_t)j] + (j - lo[(size_t)j]))] = 1.0;

    // correlations with the already-decoded in-window neighbours below j; store
    // both (j,k) and (k,j) since the full symmetric band is kept.
    const double* uj = cols[(size_t)j].data();
    for (R_xlen_t k = lo[(size_t)j]; k < j; k++) {
      const std::vector<double>& ck = cols[(size_t)k];
      if (ck.empty()) continue;  // defensive; k >= lo[j] means not yet evicted
      const double* uk = ck.data();
      double r = 0.0;
      for (R_xlen_t i = 0; i < n; i++) r += uj[(size_t)i] * uk[(size_t)i];
      if (thr_r2 > 0.0 && r * r < thr_r2) r = 0.0;
      rvals[(size_t)(cum[(size_t)k] + (j - lo[(size_t)k]))] = r;  // (row j, col k)
      rvals[(size_t)(cum[(size_t)j] + (k - lo[(size_t)j]))] = r;  // (row k, col j)
    }

    if ((j & 0x3FF) == 0) R_CheckUserInterrupt();
  }

  SEXP runtime = Rfmalloc_runtime_of_vector(tensor);
  const int window = has_pos ? (int)W : (int)size_idx;
  SEXP payload = PROTECT(Rfmalloc_ld_build(runtime, m, bits, window,
                                           lo.data(), len.data(), rvals.data()));
  UNPROTECT(1);
  return payload;
}

static const R_CallMethodDef CallEntries[] = {
    {"C_statgen_pca_incore", (DL_FUNC)&C_statgen_pca_incore, 7},
    {"C_statgen_pca_ooc", (DL_FUNC)&C_statgen_pca_ooc, 9},
    {"C_statgen_snp_cor", (DL_FUNC)&C_statgen_snp_cor, 7},
    {NULL, NULL, 0}};

extern "C" void R_init_RfmallocStatgen(DllInfo* dll) {
  R_registerRoutines(dll, NULL, CallEntries, NULL, NULL);
  R_useDynamicSymbols(dll, FALSE);
}
