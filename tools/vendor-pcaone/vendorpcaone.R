#!/usr/bin/env Rscript
# vendor PCAone's randomized-SVD core into packages/RfmallocStatgen/src/pcaone,
# pinned at one exact upstream commit. Same shape as
# tools/vendor-pgenlib/vendorpgen.R and tools/vendor-plink2-import/
# vendorplink2import.R (self-locating, mode-driven, pins an immutable upstream
# artifact, asserts byte-equal in `check` mode). This is RfmallocStatgen's path
# to a streaming out-of-core PCA: it OWNS the vendored RSVD workhorse as
#
#     (SHA-pinned PCAone commit tarball)
#       - Halko.{hpp,cpp}  RsvdOpData::computeUSV, the block-wise power-iteration
#       - Data.{hpp,cpp}   the read_block_initial/read_block_update abstraction
#       - RSVD.hpp         the header-only Eigen RSVD primitives
#       - Common.hpp Cmd.hpp Logger.hpp Timer.hpp   the small header closure
#       + deterministic R-package adaptations described below
#
# recorded here and verifiable by re-running this script.
#
# WHAT IS VENDORED (this script, byte-checked in `check` mode):
#   verbatim : Halko.hpp Halko.cpp Data.hpp Data.cpp RSVD.hpp Timer.hpp Cmd.hpp
#              - copied byte-for-byte from the pinned commit.
#   trimmed  : Common.hpp - copied verbatim EXCEPT the two consecutive lines
#              "#define ZSTD_STATIC_LINKING_ONLY" and #include "zstd.h". PCAone
#              pulls zstd in through Common.hpp for its compressed readers;
#              RfmallocStatgen brings its own Rfmalloc storage and never touches
#              zstd, and nothing in Common.hpp itself uses it. Removing exactly
#              those two lines is deterministic, so the trimmed result is still
#              byte-checked against the recipe.
#   adapted  : Logger.hpp - initializes its screen flag to false and routes the
#              optional ostream-compatible screen sink through Rprintf/REprintf
#              instead of std::cout/std::cerr. This removes undefined behaviour
#              from the upstream default constructor and keeps output on R's
#              console. The transformation asserts every edited anchor.
#
# WHAT IS NOT VENDORED BY THIS SCRIPT (hand-authored, NOT byte-checked - the same
# rule vendorpgen.R applies to its Makevars):
#   src/pcaone/Utils.hpp, src/pcaone/Utils.cpp
#       PCAone's Utils.{hpp,cpp} drag in zlib, zstd, kfunc and the bgen/beagle/
#       csv readers - all on this vendoring's SKIP list. Halko.cpp and Data.cpp
#       only reference a handful of free helpers from that unit (cao, tick,
#       mev, minSSE, flip_UV, write_eigvecs2_beagle, read_frq,
#       pvar_line_to_bim_line, split_string). RfmallocStatgen ships a trimmed
#       Utils.{hpp,cpp} that declares/defines exactly that subset - the real
#       algorithm bodies copied verbatim from upstream, the beagle-output stub
#       inert - so the vendored Halko.cpp/Data.cpp compile and link unmodified
#       without the skipped dependencies. See src/pcaone/Utils.hpp's header.
#   src/pcaone_backend.{h,cpp}, src/statgen_glue.cpp, src/Makevars*
#       RfmallocStatgen's own backend, R glue, and build config.
#
# The Param(int,char**) constructor Cmd.hpp declares lives in PCAone's Cmd.cpp
# (a popl-based CLI parser, on the SKIP list); we keep Cmd.hpp verbatim and
# supply a trivial Param(int,char**){} in src/pcaone_backend.cpp instead, so the
# struct's in-class member defaults are the only configuration surface.
#
# Usage (from anywhere):
#   Rscript tools/vendor-pcaone/vendorpcaone.R download  # fetch + SHA-verify into cache/
#   Rscript tools/vendor-pcaone/vendorpcaone.R vendor    # regenerate the vendored core in place
#   Rscript tools/vendor-pcaone/vendorpcaone.R check     # regenerate to a temp dir, diff vs the committed tree

args <- commandArgs(trailingOnly = TRUE)
mode <- if (length(args) > 0) args[[1]] else "check"

## --- pinned base -------------------------------------------------------------
pcaone_sha     <- "e36ba7612e0aa2142e01add3dd35dbdbf90acc7e"
tarball_sha256 <- "173209491dc8f4053cf462db7b8a6580991b3a51217d4ce92e4b8d99ba22f578"
tarball        <- sprintf("PCAone-%s.tar.gz", pcaone_sha)
tarball_url    <- sprintf("https://github.com/Zilong-Li/PCAone/archive/%s.tar.gz", pcaone_sha)
# GitHub's own tarball unpacks into a directory named "PCAone-<sha>".
tar_root_name  <- sprintf("PCAone-%s", pcaone_sha)

## --- locate ourselves (as vendorpgen.R / vendorplink2import.R do) ------------
getScriptPath <- function() {
  cmd.args <- commandArgs()
  m <- regexpr("(?<=^--file=).+", cmd.args, perl = TRUE)
  script.dir <- dirname(regmatches(cmd.args, m))
  if (length(script.dir) != 1) {
    stop("can't determine script dir: call this with Rscript")
  }
  normalizePath(script.dir)
}
here    <- getScriptPath()                              # tools/vendor-pcaone
repo    <- normalizePath(file.path(here, "..", ".."))    # monorepo root
pkg_dst <- file.path(repo, "packages", "RfmallocStatgen")
cache   <- file.path(here, "cache")

# Files copied byte-for-byte from the pinned commit's src/ into
# packages/RfmallocStatgen/src/pcaone/.
verbatim_files <- c(
  "Halko.hpp", "Halko.cpp",
  "Data.hpp", "Data.cpp",
  "RSVD.hpp",
  "Timer.hpp",
  "Cmd.hpp"
)
# Common.hpp gets the one deterministic trim described in the header.
trimmed_file <- "Common.hpp"
adapted_file <- "Logger.hpp"

sha256 <- function(path) unname(tools::sha256sum(path))

fetch_base <- function() {
  dir.create(cache, showWarnings = FALSE, recursive = TRUE)
  dst <- file.path(cache, tarball)
  if (!file.exists(dst)) {
    ok <- tryCatch({
      download.file(tarball_url, dst, quiet = TRUE, mode = "wb")
      TRUE
    }, error = function(e) FALSE)
    if (!ok) stop("could not download ", tarball, " from ", tarball_url)
  }
  got <- sha256(dst)
  if (!identical(got, tarball_sha256)) {
    stop(sprintf("%s sha256 mismatch\n  expected %s\n  got      %s",
                 tarball, tarball_sha256, got))
  }
  message("base OK: ", tarball, " (sha256 ", substr(got, 1, 12), "...)")
  dst
}

if (mode == "download") {
  fetch_base()
  quit(save = "no")
}

if (!(mode %in% c("vendor", "check"))) {
  stop("Unknown mode: ", mode, ". Use 'download', 'vendor', or 'check'.")
}

## --- the one deterministic trim ----------------------------------------------
# Remove exactly the two consecutive zstd lines from Common.hpp; error out if
# they are not found exactly once (so an upstream change to that region is a
# loud failure, not a silent no-op).
trim_common <- function(lines) {
  def_idx <- grep("^#define ZSTD_STATIC_LINKING_ONLY$", lines)
  inc_idx <- grep('^#include "zstd.h"$', lines)
  if (length(def_idx) != 1 || length(inc_idx) != 1) {
    stop("Common.hpp: expected exactly one zstd define and one zstd include, found ",
         length(def_idx), " and ", length(inc_idx))
  }
  if (inc_idx != def_idx + 1) {
    stop("Common.hpp: zstd #define and #include are no longer consecutive; ",
         "re-check the trim in vendorpcaone.R")
  }
  lines[-c(def_idx, inc_idx)]
}

adapt_logger <- function(lines) {
  guard <- grep("^#define LOG_H_$", lines)
  include_end <- grep("^#include <sstream>$", lines)
  flag <- grep("^  bool is_screen;$", lines)
  ctor <- grep("^  Logger\\(\\) \\{\\}$", lines)
  if (length(guard) != 1 || length(include_end) != 1 ||
      length(flag) != 1 || length(ctor) != 1) {
    stop("Logger.hpp: R-console adaptation anchors changed upstream")
  }

  lines <- append(lines, "#include <R_ext/Print.h>", after = guard)
  include_end <- grep("^#include <sstream>$", lines)
  bridge <- c(
    "",
    "class RConsoleBuffer : public std::streambuf {",
    " public:",
    "  explicit RConsoleBuffer(bool error) : error_(error) {}",
    "",
    " protected:",
    "  int_type overflow(int_type ch) override {",
    "    if (traits_type::eq_int_type(ch, traits_type::eof()))",
    "      return traits_type::not_eof(ch);",
    "    const char c = traits_type::to_char_type(ch);",
    "    xsputn(&c, 1);",
    "    return ch;",
    "  }",
    "",
    "  std::streamsize xsputn(const char* text, std::streamsize size) override {",
    "    if (size <= 0) return 0;",
    "    const std::string value(text, static_cast<size_t>(size));",
    "    if (error_)",
    "      REprintf(\"%s\", value.c_str());",
    "    else",
    "      Rprintf(\"%s\", value.c_str());",
    "    return size;",
    "  }",
    "",
    " private:",
    "  bool error_;",
    "};",
    "",
    "inline RConsoleBuffer r_stdout_buffer(false);",
    "inline RConsoleBuffer r_stderr_buffer(true);",
    "inline std::ostream r_cout(&r_stdout_buffer);",
    "inline std::ostream r_cerr(&r_stderr_buffer);"
  )
  lines <- append(lines, bridge, after = include_end)
  lines[grep("^  bool is_screen;$", lines)] <- "  bool is_screen = false;"
  lines[grep("^  Logger\\(\\) \\{\\}$", lines)] <- "  Logger() = default;"
  if (!any(grepl("std::cout", lines, fixed = TRUE)) ||
      !any(grepl("std::cerr", lines, fixed = TRUE))) {
    stop("Logger.hpp: expected std::cout and std::cerr uses were not found")
  }
  lines <- gsub("std::cout", "r_cout", lines, fixed = TRUE)
  lines <- gsub("std::cerr", "r_cerr", lines, fixed = TRUE)
  if (any(grepl("std::cout|std::cerr", lines))) {
    stop("Logger.hpp: incomplete R-console adaptation")
  }
  lines
}

## --- regenerate --------------------------------------------------------------
tb   <- fetch_base()
work <- tempfile("vendorpcaone-")
dir.create(work)
on.exit(unlink(work, recursive = TRUE), add = TRUE)
utils::untar(tb, exdir = work)
src <- file.path(work, tar_root_name, "src")
if (!dir.exists(src)) {
  stop("unexpected tarball layout: ", src, " does not exist")
}

if (mode == "check") {
  out <- file.path(work, "out")
} else {
  out <- pkg_dst
}
out_pcaone <- file.path(out, "src", "pcaone")
dir.create(out_pcaone, showWarnings = FALSE, recursive = TRUE)

message("copy verbatim RSVD core (", length(verbatim_files), " files -> src/pcaone/)")
for (f in verbatim_files) {
  stopifnot(file.copy(file.path(src, f), file.path(out_pcaone, f), overwrite = TRUE))
}

message("copy Common.hpp with the zstd-include trim")
common_lines <- readLines(file.path(src, trimmed_file))
writeLines(trim_common(common_lines), file.path(out_pcaone, trimmed_file))

message("copy Logger.hpp with the R-console adaptation")
logger_lines <- readLines(file.path(src, adapted_file))
writeLines(adapt_logger(logger_lines), file.path(out_pcaone, adapted_file))

if (mode == "check") {
  mism <- character(0)
  check_file <- function(rel) {
    a <- file.path(out_pcaone, rel)
    b <- file.path(pkg_dst, "src", "pcaone", rel)
    if (!file.exists(b) || !identical(sha256(a), sha256(b))) mism <<- c(mism, rel)
  }
  for (f in verbatim_files) check_file(f)
  check_file(trimmed_file)
  check_file(adapted_file)

  if (length(mism) == 0) {
    message("OK: committed vendored PCAone core == vendorpcaone.R output (",
            length(verbatim_files) + 1L,
            " files plus Logger.hpp; Utils.{hpp,cpp} hand-authored, not checked)")
  } else {
    stop("MISMATCH - committed tree no longer equals the recipe:\n  ",
         paste(mism, collapse = "\n  "))
  }
} else {
  message("regenerated packages/RfmallocStatgen/src/pcaone from PCAone ", pcaone_sha)
}
