/*
 * R interface for RfmallocStatgen's native code. Uses R's C API directly (no
 * Rcpp) and includes only pcaone_backend.h - never the PCAone headers - so R's
 * macro remaps and PCAone's C++ never share a translation unit. GPL-3.
 */
#define R_NO_REMAP
#include <R.h>
#include <Rinternals.h>
#include <R_ext/Rdynload.h>

#include <cstddef>
#include <cstring>
#include <exception>

#include "pcaone_backend.h"

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
    // The C++ stack has fully unwound here, so it is safe to longjmp out.
    Rf_error("statgen_pca: %s", e.what());
  } catch (...) {
    Rf_error("statgen_pca: unknown C++ exception in RSVD backend");
  }

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

static const R_CallMethodDef CallEntries[] = {
    {"C_statgen_pca_incore", (DL_FUNC)&C_statgen_pca_incore, 7},
    {NULL, NULL, 0}};

extern "C" void R_init_RfmallocStatgen(DllInfo* dll) {
  R_registerRoutines(dll, NULL, CallEntries, NULL, NULL);
  R_useDynamicSymbols(dll, FALSE);
}
