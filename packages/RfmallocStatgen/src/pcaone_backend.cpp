/*
 * Drives the vendored PCAone RSVD core (src/pcaone/) over an in-memory matrix.
 * No R headers here on purpose - see pcaone_backend.h. GPL-3.
 */
#include "pcaone_backend.h"

#include "pcaone/Halko.hpp"  // -> Data.hpp -> Cmd.hpp, Common.hpp, RSVD.hpp

// PCAone declares Param(int,char**)/~Param() in Cmd.hpp and defines them in
// Cmd.cpp (a popl-based CLI parser we deliberately do not vendor). Supply
// trivial definitions so the vendored units link; the struct's in-class member
// defaults (Cmd.hpp) are the only configuration RfmallocStatgen relies on.
Param::Param(int, char**) {}
Param::~Param() {}

namespace {

// Concrete in-core Data: the entire matrix already lives in G (nsamples x
// nsnps), so the file-streaming virtuals are inert. This is exactly the operand
// RsvdOpData::computeGandH consumes in its !params.out_of_core branch.
class InCoreData : public Data {
 public:
  InCoreData(const Param& p, const double* X, int n, int m) : Data(p) {
    nsamples = static_cast<uint>(n);
    nsnps = static_cast<uint>(m);
    snpmajor = true;
    // X is column-major (R/Eigen); Mat2D is column-major too, so this is a
    // straight copy with no transpose.
    G = Eigen::Map<const Mat2D>(X, n, m);
  }
  void read_all() override {}
  void check_file_offset_first_var() override {}
  void read_block_initial(uint64, uint64, bool) override {}
  void read_block_update(uint64, uint64, const Mat2D&, const Mat1D&, const Mat2D&, bool) override {}
};

rfmstatgen::RsvdResult pack(const Mat1D& S, const Mat2D& U, const Mat2D& V) {
  rfmstatgen::RsvdResult r;
  r.n = static_cast<int>(U.rows());
  r.m = static_cast<int>(V.rows());
  r.k = static_cast<int>(S.size());
  r.d.assign(S.data(), S.data() + S.size());
  r.u.assign(U.data(), U.data() + U.size());
  r.v.assign(V.data(), V.data() + V.size());
  return r;
}

}  // namespace

namespace rfmstatgen {

RsvdResult rsvd_incore(const double* X, int n, int m, int k, int p, int s,
                       int method, double tol, int seed) {
  Param params(0, nullptr);
  // Everything below either sets a member PCAone leaves uninitialised (svd_t,
  // file_t) or silences behaviour we do not want on the numeric path.
  params.verbose = 0;             // no cao.* logging on this path
  params.svd_t = (method == 2) ? SvdType::PCAoneAlg2 : SvdType::PCAoneAlg1;
  params.file_t = FileType::CSV;  // unused, but Param does not default it
  params.out_of_core = false;
  params.seed = seed;
  params.rand = 1;                // Gaussian (StandardNormal) test matrix

  InCoreData data(params, X, n, m);
  RsvdOpData* op = nullptr;
  if (method == 2)
    op = new FancyRsvdOpData(&data, k, s);
  else
    op = new NormalRsvdOpData(&data, k, s);

  // update = false (raw decomposition), standardize = false (any centering /
  // scaling is applied to X before it reaches this backend).
  op->setFlags(false, false);
  op->computeUSV(p, tol);

  // Storage convention (see Halko.cpp::computeUSV): with G = X (nsamples x
  // nsnps), op->U holds the left singular vectors (nsamples x k), op->V the
  // right singular vectors (nsnps x k), op->S the singular values, so
  // X ~ U diag(S) V^T - exactly svd(X)'s u/d/v.
  RsvdResult r = pack(op->S, op->U, op->V);
  delete op;
  return r;
}

}  // namespace rfmstatgen
