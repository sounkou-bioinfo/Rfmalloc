/*******************************************************************************
 * @file        packages/RfmallocStatgen/src/pcaone/Utils.hpp
 *
 * TRIMMED derivative of https://github.com/Zilong-Li/PCAone/src/Utils.hpp
 * (Copyright (C) 2022-2024 Zilong Li, GPL-3). This is NOT the verbatim upstream
 * header: PCAone's Utils.{hpp,cpp} pull in zlib, zstd, kfunc and the
 * bgen/beagle/csv readers, all of which RfmallocStatgen deliberately does not
 * vendor. The vendored Halko.cpp and Data.cpp reference only a small set of
 * free helpers from that unit; this header declares exactly that set - plus the
 * cao/tick toolbox globals PCAone shares through Utils.hpp - so those two
 * translation units compile and link unchanged against the trimmed Utils.cpp
 * in this directory. See tools/vendor-pcaone/vendorpcaone.R for the split
 * between the byte-checked vendored core and this hand-authored shim.
 ******************************************************************************/
#ifndef PCAONE_UTILES_
#define PCAONE_UTILES_

#include "Common.hpp"
#include "Logger.hpp"
#include "Timer.hpp"

// MAKE SOME TOOLS FULLY ACCESSIBLE THROUGHOUT THE SOFTWARE
// (same toolbox pattern as upstream Utils.hpp: the single translation unit that
// defines _DECLARE_TOOLBOX_HERE - here Utils.cpp - owns the definitions.)
#ifdef _DECLARE_TOOLBOX_HERE
Logger cao;  // logger
Timer tick;  // Timer
#else
extern Timer tick;
extern Logger cao;
#endif

// --- the subset Halko.cpp and Data.cpp reference ----------------------------
// Bodies are copied verbatim from upstream Utils.cpp in this directory's
// Utils.cpp, except write_eigvecs2_beagle(), which upstream routes through the
// (skipped) gzip/beagle reader and is therefore an inert stub here - the code
// path that would call it (pcangsd + BEAGLE input) is unreachable from
// RfmallocStatgen's numeric entry points.

Mat1D minSSE(const Mat2D& X, const Mat2D& Y);

double mev(const Mat2D& X, const Mat2D& Y);

void flip_UV(Mat2D& U, Mat2D& V, bool ubase = true);

String1D split_string(const std::string& s, const std::string& separators);

Mat1D read_frq(const std::string& path);

std::string pvar_line_to_bim_line(const std::string& line, const std::string& path);

void write_eigvecs2_beagle(const Mat2D& U, const std::string& fin, const std::string& fout);

#endif  // PCAONE_UTILES_
