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

// A caller-supplied decoder for one variant-column range of an opaque tensor:
// writes n_elems doubles (column-major, nsamples-fastest) for the flat element
// range [elem_offset, elem_offset + n_elems) into out; returns 0 on success.
// This is Rfmalloc_tensor_decode bound to a tensor, expressed R-free so the
// PCAone backend links without R headers (the glue supplies the adapter).
typedef int (*decode_range_fn)(void* tensor, long long elem_offset,
                               long long n_elems, double* out);

// Out-of-core randomized SVD (single-pass sSVD) of an n x m (samples x variants)
// tensor, streamed one variant-column block at a time via decode: the full
// n x m double matrix is never materialized, only one n x block_size panel at a
// time. Standardization is a property of the tensor (pass a standardized tensor
// to get a standardized PCA), so it is baked into what decode returns. Produces
// the same triplet as rsvd_incore() on the fully decoded matrix, up to
// block-summation order. block_size is the number of variants per streamed
// block (>= 1). May throw std::exception (e.g. on a decode failure).
RsvdResult rsvd_ooc(void* tensor, decode_range_fn decode, int n, int m,
                    int k, int p, int s, double tol, int seed, int block_size);

}  // namespace rfmstatgen

#endif  // RFMSTATGEN_PCAONE_BACKEND_H
