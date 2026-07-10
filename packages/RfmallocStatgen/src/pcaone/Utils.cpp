/*******************************************************************************
 * @file        packages/RfmallocStatgen/src/pcaone/Utils.cpp
 *
 * TRIMMED derivative of https://github.com/Zilong-Li/PCAone/src/Utils.cpp
 * (Copyright (C) 2022-2024 Zilong Li, GPL-3). Only the free helpers that the
 * vendored Halko.cpp and Data.cpp actually reference are kept; their bodies are
 * copied VERBATIM from upstream Utils.cpp so the numeric behaviour is identical.
 * The single exception is write_eigvecs2_beagle(), which upstream implements on
 * top of the (skipped) gzip/beagle reader: here it is an inert stub, because the
 * only caller (the pcangsd + BEAGLE branch of run_pca_with_halko) is never
 * reached from RfmallocStatgen. This unit also owns the cao/tick toolbox globals
 * (via _DECLARE_TOOLBOX_HERE), as upstream Utils.cpp does. See Utils.hpp and
 * tools/vendor-pcaone/vendorpcaone.R for why this file is hand-authored rather
 * than vendored byte-for-byte.
 ******************************************************************************/

#define _DECLARE_TOOLBOX_HERE

#include "Utils.hpp"

#include <fstream>
#include <stdexcept>

#include "Common.hpp"

using namespace std;

// Sign correction to ensure deterministic output from SVD.
// see https://www.kite.com/python/docs/sklearn.utils.extmath.svd_flip
void flip_UV(Mat2D& U, Mat2D& V, bool ubase) {
  if (ubase) {
    Eigen::Index x, i;
    for (i = 0; i < U.cols(); ++i) {
      U.col(i).cwiseAbs().maxCoeff(&x);
      if (U(x, i) < 0) {
        U.col(i) *= -1;
        if (V.cols() == U.cols()) {
          V.col(i) *= -1;
        } else if (V.rows() == U.cols()) {
          V.row(i) *= -1;
        } else {
          cao.error("the dimention of U and V have different k ranks.\n");
        }
      }
    }
  } else {
    Eigen::Index x, i;
    for (i = 0; i < V.cols(); ++i) {
      if (V.cols() == U.cols()) {
        V.col(i).cwiseAbs().maxCoeff(&x);
        if (V(x, i) < 0) {
          U.col(i) *= -1;
          V.col(i) *= -1;
        }
      } else if (V.rows() == U.cols()) {
        V.row(i).cwiseAbs().maxCoeff(&x);
        if (V(i, x) < 0) {
          U.col(i) *= -1;
          V.row(i) *= -1;
        }
      } else {
        cao.error("the dimention of U and V have different k ranks.\n");
      }
    }
  }
}

// Y is the truth matrix, X is the test matrix
Mat1D minSSE(const Mat2D& X, const Mat2D& Y) {
  Eigen::Index w1, w2;
  Mat1D res(X.cols());
  for (Eigen::Index i = 0; i < X.cols(); ++i) {
    // test against the original matrix to find the index with mincoeff
    ((-Y).colwise() + X.col(i)).array().square().colwise().sum().minCoeff(&w1);
    // test against the flipped matrix with the opposite sign
    (Y.colwise() + X.col(i)).array().square().colwise().sum().minCoeff(&w2);
    // get the minSSE value for X.col(i) against -Y.col(w1)
    auto val1 = (-Y.col(w1) + X.col(i)).array().square().sum();
    // get the minSSE value for X.col(i) against Y.col(w2)
    auto val2 = (Y.col(w2) + X.col(i)).array().square().sum();
    if (w1 != w2 && val1 > val2)
      res[i] = val2;
    else
      res[i] = val1;
  }
  return res;
}

double mev(const Mat2D& X, const Mat2D& Y) {
  double res = 0;
  for (Eigen::Index i = 0; i < X.cols(); ++i) {
    res += (X.transpose() * Y.col(i)).norm();
  }
  return res / X.cols();
}

String1D split_string(const std::string& s, const std::string& separators) {
  String1D ret;
  bool is_seperator[256] = {false};
  for (auto& ch : separators) {
    is_seperator[(unsigned int)ch] = true;
  }
  int begin = 0;
  for (int i = 0; i <= (int)s.size(); i++) {
    if (is_seperator[(uint8_t)s[i]] || i == (int)s.size()) {
      ret.push_back(std::string(s.begin() + begin, s.begin() + i));
      begin = i + 1;
    }
  }
  return ret;
}

// parse AF
Mat1D read_frq(const std::string& path) {
  const std::string sep{" \t"};
  double val;
  Double1D V;
  std::ifstream fin(path);
  std::string line;
  while (getline(fin, line)) {
    auto tokens = split_string(line, sep);
    if ((int)tokens.size() != 7) cao.error("the input file is not valid!\n => " + path);
    val = std::stod(tokens[6]);
    V.push_back(val);
  }
  return Eigen::Map<Mat1D>(V.data(), V.size());
}

namespace {
String1D pvar_tokens_to_bim_tokens(const String1D& tokens, const std::string& path, const std::string& line) {
  if ((int)tokens.size() < 5) cao.error("the input pvar file is not valid!\n => " + path + "\n" + line);
  std::string alt = tokens[4];
  size_t comma = alt.find(',');
  if (comma != std::string::npos) alt = alt.substr(0, comma);
  // PGEN reader uses ALT allele dosage (allele_idx=1), so expose ALT as BIM A1.
  return String1D{tokens[0], tokens[2], "0", tokens[1], alt, tokens[3]};
}
}  // namespace

std::string pvar_line_to_bim_line(const std::string& line, const std::string& path) {
  const std::string sep{" \t"};
  auto tokens = pvar_tokens_to_bim_tokens(split_string(line, sep), path, line);
  return tokens[0] + "\t" + tokens[1] + "\t" + tokens[2] + "\t" + tokens[3] + "\t" + tokens[4] + "\t" + tokens[5];
}

// Upstream writes eigenvectors in BEAGLE sample order via the gzip beagle
// reader, which RfmallocStatgen does not vendor. The only caller is the
// pcangsd + BEAGLE branch of run_pca_with_halko(), unreachable here; keep an
// inert stub so the vendored Halko.cpp links.
void write_eigvecs2_beagle(const Mat2D&, const std::string&, const std::string&) {
  throw std::runtime_error("write_eigvecs2_beagle: BEAGLE output is not supported in RfmallocStatgen");
}
