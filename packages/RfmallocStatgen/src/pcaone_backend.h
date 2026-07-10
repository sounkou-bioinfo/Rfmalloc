/*
 * RfmallocStatgen native backend: a thin, R-free wrapper around the vendored
 * PCAone randomized-SVD core (src/pcaone/). Kept free of any R/Rcpp headers so
 * the PCAone translation units never see R's macro remaps (length, error, ...);
 * the R interface lives in statgen_glue.cpp and talks to this header only.
 *
 * GPL-3 (this package). The vendored PCAone core it drives is GPL-3 as well;
 * see src/pcaone/ and tools/vendor-pcaone/vendorpcaone.R.
 */
#ifndef RFMSTATGEN_PCAONE_BACKEND_H
#define RFMSTATGEN_PCAONE_BACKEND_H

#include <vector>

namespace rfmstatgen {

// Top-k singular triplet of an n x m matrix X: X ~ u * diag(d) * v^T.
// u and v are stored column-major (Eigen/R order); d has length k.
struct RsvdResult {
  int n = 0;  // number of samples (rows of X, rows of u)
  int m = 0;  // number of variants (cols of X, rows of v)
  int k = 0;  // rank returned
  std::vector<double> d;  // length k
  std::vector<double> u;  // n * k, column-major
  std::vector<double> v;  // m * k, column-major
};

// In-core randomized SVD of the n x m column-major matrix X (samples x
// variants). p = power iterations, s = oversamples, tol = subspace-convergence
// tolerance, seed = RNG seed for the Gaussian test matrix. method 1 = single-
// pass sSVD (PCAone NormalRsvdOpData), method 2 = windowed winSVD
// (FancyRsvdOpData). May throw std::exception on misuse; the caller (glue)
// translates that into an R error after the C++ stack has unwound.
RsvdResult rsvd_incore(const double* X, int n, int m, int k, int p, int s,
                       int method, double tol, int seed);

}  // namespace rfmstatgen

#endif  // RFMSTATGEN_PCAONE_BACKEND_H
