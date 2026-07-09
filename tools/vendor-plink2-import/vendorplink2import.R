#!/usr/bin/env Rscript
# vendor plink2's VCF/BCF import closure (VcfToPgen and friends) from
# plink-ng, pinned at one exact upstream commit. Same shape as
# tools/vendor-pgenlib/vendorpgen.R (self-locating, mode-driven, pins an
# immutable upstream artifact, applies a small local patch), extended for the
# *program* subset of plink2 (as opposed to vendorpgen.R's *library*/pgenlib
# read subset). This is milestone 4a's path to VcfToPgen():
#
#     packages/Rpgen/tools/include/{program files, new}
#       + packages/Rpgen/tools/include/include/{pgenlib_write,SFMT, new}
#     =  (SHA-pinned plink-ng commit tarball)
#      + (one local patch: plink2_cmdline.cc logging -> R I/O)
#
# See PROVENANCE.md for why these exact files and no others, and why the
# vendored subset lands one directory level up from vendorpgen.R's pgenlib
# read subset (tools/include/ vs. tools/include/include/ - mirrors plink-ng's
# own 2.0/ vs. 2.0/include/ split between "program" and "library").
#
# Ordering with vendorpgen.R: vendorpgen.R's own "vendor" mode wipes and
# regenerates the *whole* tools/include/ directory from the pgenlibr tarball
# (which does not contain any of the files this script adds). Always run
# vendorpgen.R first if both need re-running; this script only ever adds/
# overwrites its own file list on top, never deletes anything outside it.
#
# Usage (from anywhere):
#   Rscript tools/vendor-plink2-import/vendorplink2import.R download  # fetch + SHA-verify into cache/
#   Rscript tools/vendor-plink2-import/vendorplink2import.R vendor    # regenerate the vendored subset in place
#   Rscript tools/vendor-plink2-import/vendorplink2import.R check     # regenerate to a temp dir, diff vs the committed tree

args <- commandArgs(trailingOnly = TRUE)
mode <- if (length(args) > 0) args[[1]] else "check"

## --- pinned base -------------------------------------------------------------
plinkng_sha    <- "a81e38220b16e3907bdcedbe6ce39b273e001e13"
tarball_sha256 <- "58e1bd94f7359acf160134d6ee80e0ea508c6cb1d8f8380a1b0b2d8ed1b1318f"
tarball        <- sprintf("plink-ng-%s.tar.gz", plinkng_sha)
tarball_url    <- sprintf("https://github.com/chrchang/plink-ng/archive/%s.tar.gz", plinkng_sha)
# GitHub's own tarball unpacks into a directory named "plink-ng-<sha>".
tar_root_name  <- sprintf("plink-ng-%s", plinkng_sha)

## --- locate ourselves (as vendorpgen.R / vendorggml.R do) -------------------
getScriptPath <- function() {
  cmd.args <- commandArgs()
  m <- regexpr("(?<=^--file=).+", cmd.args, perl = TRUE)
  script.dir <- dirname(regmatches(cmd.args, m))
  if (length(script.dir) != 1) {
    stop("can't determine script dir: call this with Rscript")
  }
  normalizePath(script.dir)
}
here    <- getScriptPath()                             # tools/vendor-plink2-import
repo    <- normalizePath(file.path(here, "..", ".."))   # monorepo root
pkg_dst <- file.path(repo, "packages", "Rpgen")
cache   <- file.path(here, "cache")
patches <- file.path(here, "patches")

# Program-level files (plink-ng's 2.0/, NOT 2.0/include/): the VcfToPgen()
# closure. Land at packages/Rpgen/tools/include/ (program level), one
# directory above vendorpgen.R's pgenlib read subset - see PROVENANCE.md for
# why (mirrors plink-ng's own program/library split: program files #include
# each other with no "include/" prefix).
program_files <- c(
  "plink2_import.cc", "plink2_import.h",
  "plink2_common.cc", "plink2_common.h",
  "plink2_cmdline.cc", "plink2_cmdline.h",
  "plink2_pvar.cc", "plink2_pvar.h",
  "plink2_psam.cc", "plink2_psam.h",
  "plink2_compress_stream.cc", "plink2_compress_stream.h",
  "plink2_import_legacy.cc", "plink2_import_legacy.h",
  "plink2_random.cc", "plink2_random.h",
  "plink2_decompress.cc", "plink2_decompress.h",
  "plink2_data.cc", "plink2_data.h",
  "plink2_family.cc", "plink2_family.h"
)
# plink2_cmdline.cc gets one local patch after copying (see below).
patched_program_files <- c("plink2_cmdline.cc")

# Library-level files (plink-ng's 2.0/include/): join vendorpgen.R's existing
# pgenlib read subset in packages/Rpgen/tools/include/include/. pgenlib_write
# is the .pgen writer VcfToPgen() calls; SFMT is the Mersenne-twister-family
# PRNG plink2_random.h wraps (used for tie-breaking, not cryptographic).
library_files <- c(
  "include/pgenlib_write.cc", "include/pgenlib_write.h",
  "include/SFMT.c", "include/SFMT.h"
)

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

## --- regenerate ---------------------------------------------------------------
tb   <- fetch_base()
work <- tempfile("vendorplink2import-")
dir.create(work)
on.exit(unlink(work, recursive = TRUE), add = TRUE)
utils::untar(tb, exdir = work)
src <- file.path(work, tar_root_name, "2.0")
if (!dir.exists(src)) {
  stop("unexpected tarball layout: ", src, " does not exist")
}

if (mode == "check") {
  out <- file.path(work, "out")
} else {
  out <- pkg_dst
}
out_tools_include <- file.path(out, "tools", "include")
dir.create(out_tools_include, showWarnings = FALSE, recursive = TRUE)
dir.create(file.path(out_tools_include, "include"), showWarnings = FALSE, recursive = TRUE)

message("copy program-level files (plink-ng 2.0/*.cc,*.h -> tools/include/)")
for (f in program_files) {
  stopifnot(file.copy(file.path(src, f), file.path(out_tools_include, f), overwrite = TRUE))
}

message("copy library-level files (plink-ng 2.0/include/* -> tools/include/include/)")
for (f in library_files) {
  stopifnot(file.copy(file.path(src, f), file.path(out_tools_include, f), overwrite = TRUE))
}

message("apply local patches (stock plink-ng -> ours)")
for (p in sort(list.files(patches, "\\.patch$", full.names = TRUE))) {
  rc <- system2("patch", c("-p1", "-s", "-d", shQuote(out_tools_include)),
                stdin = p)
  if (rc != 0) stop("patch failed: ", basename(p))
  message("  applied ", basename(p))
}

if (mode == "check") {
  mism <- character(0)

  check_file <- function(rel) {
    a <- file.path(out_tools_include, rel)
    b <- file.path(pkg_dst, "tools", "include", rel)
    if (!file.exists(b) || !identical(sha256(a), sha256(b))) mism <<- c(mism, rel)
  }
  for (f in program_files) check_file(f)
  for (f in library_files) check_file(f)

  if (length(mism) == 0) {
    message("OK: committed vendored plink2-import subset == vendorplink2import.R output (",
            length(program_files) + length(library_files), " files)")
  } else {
    stop("MISMATCH - committed tree no longer equals the recipe:\n  ",
         paste(mism, collapse = "\n  "))
  }
} else {
  message("regenerated packages/Rpgen tools/include/ VcfToPgen() closure from plink-ng ", plinkng_sha)
}
