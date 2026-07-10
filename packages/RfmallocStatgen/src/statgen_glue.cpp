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
#include <exception>

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

static const R_CallMethodDef CallEntries[] = {
    {"C_statgen_pca_incore", (DL_FUNC)&C_statgen_pca_incore, 7},
    {"C_statgen_pca_ooc", (DL_FUNC)&C_statgen_pca_ooc, 9},
    {NULL, NULL, 0}};

extern "C" void R_init_RfmallocStatgen(DllInfo* dll) {
  R_registerRoutines(dll, NULL, CallEntries, NULL, NULL);
  R_useDynamicSymbols(dll, FALSE);
}
