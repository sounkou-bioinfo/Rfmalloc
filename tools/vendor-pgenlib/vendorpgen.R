#!/usr/bin/env Rscript
# vendor pgenlib: reproducibly regenerate Rpgen's vendored PLINK2 pgenlib tree
# from a pinned, immutable source. Same shape as tools/vendor-ggml/vendorggml.R
# (self-locating, mode-driven, pins an exact upstream artifact), adapted for
# pgenlibr's autoconf-based build. This is how the project OWNS its path to
# pgenlib: the vendored tree is not a mystery copy, it is
#
#     (SHA-pinned pgenlibr CRAN tarball) + (one local edit: OBJECTS=)
#
# recorded beside this script and verifiable by re-running it.
#
#   base  : pgenlibr <VER> source tarball from CRAN, SHA256-pinned. pgenlibr
#           is a CRAN-accepted package that already vendors exactly the
#           pgenlib *read* subset + zstd + libdeflate + simde and builds
#           libPLINK2.a from them; that build is our reference. We take its
#           configure/configure.ac/Makevars* and its whole tools/include/
#           tree verbatim. We do NOT take pgenlibr's pvar.cpp/pvar.h: they are
#           its Rcpp R-facing wrapper, and Rpgen is not an Rcpp package (it uses
#           the C FFI in include/pvar_ffi_support.*). R's Windows build compiles
#           every src/*.cpp regardless of OBJECTS, so shipping them broke the
#           Windows build on a missing Rcpp.h.
#   edit  : Makevars.in and Makevars.win each set one line,
#           "OBJECTS = pvar.o pgenlibr.o RcppExports.o", to the R-level Rcpp
#           bindings pgenlibr compiles. Rpgen has no Rcpp bindings - it has
#           its own src/rpgen.cpp (hand-authored, not vendored, not touched
#           by this script) - so that line becomes "OBJECTS = rpgen.o".
#           Applied inline below rather than as a patches/*.patch file: it is
#           a single deterministic line replacement, not a source edit.
#
# Usage (from anywhere):
#   Rscript tools/vendor-pgenlib/vendorpgen.R download   # fetch + SHA-verify the tarball into cache/
#   Rscript tools/vendor-pgenlib/vendorpgen.R vendor     # regenerate the vendored subset in place
#   Rscript tools/vendor-pgenlib/vendorpgen.R check      # regenerate to a temp dir, diff vs the committed tree

args <- commandArgs(trailingOnly = TRUE)
mode <- if (length(args) > 0) args[[1]] else "check"

## --- pinned base -------------------------------------------------------------
pgenlibr_ver <- "0.6.2"
pgenlibr_sha <- "e73520388a0d275d5af07befaa051bedef4172875d68f61f035f3e84275262bf"
tarball      <- sprintf("pgenlibr_%s.tar.gz", pgenlibr_ver)
cran_urls <- c(
  sprintf("https://cran.r-project.org/src/contrib/%s", tarball),
  sprintf("https://cran.r-project.org/src/contrib/Archive/pgenlibr/%s", tarball)
)

## --- locate ourselves (as vendorggml.R does) --------------------------------
getScriptPath <- function() {
  cmd.args <- commandArgs()
  m <- regexpr("(?<=^--file=).+", cmd.args, perl = TRUE)
  script.dir <- dirname(regmatches(cmd.args, m))
  if (length(script.dir) != 1) {
    stop("can't determine script dir: call this with Rscript")
  }
  normalizePath(script.dir)
}
here    <- getScriptPath()                             # tools/vendor-pgenlib
repo    <- normalizePath(file.path(here, "..", ".."))   # monorepo root
pkg_dst <- file.path(repo, "packages", "Rpgen")
cache   <- file.path(here, "cache")

# Files copied byte-identical from the pgenlibr tarball (package root and
# src/); Makevars.in/Makevars.win get the one OBJECTS= edit described above.
root_files <- c("configure", "configure.ac", "cleanup")
src_files_verbatim <- c("Makevars.ucrt")
src_files_patched   <- c("Makevars.in", "Makevars.win")
tools_files <- c("zstd_version.cpp", "libdeflate_version.cpp", "simde_version.cpp")
# pgenlibr's own test fixture (a small real .pgen + its .pvar.zst/.psam
# siblings), used as the oracle input for inst/tinytest/test_open.R.
extdata_files <- c("chr21_phase3_start.pgen", "chr21_phase3_start.pvar.zst",
                    "chr21_phase3_start.psam")

sha256 <- function(path) unname(tools::sha256sum(path))

fetch_base <- function() {
  dir.create(cache, showWarnings = FALSE, recursive = TRUE)
  dst <- file.path(cache, tarball)
  if (!file.exists(dst)) {
    ok <- FALSE
    for (u in cran_urls) {
      if (tryCatch({ download.file(u, dst, quiet = TRUE, mode = "wb"); TRUE },
                   error = function(e) FALSE)) { ok <- TRUE; break }
    }
    if (!ok) stop("could not download ", tarball, " from CRAN")
  }
  got <- sha256(dst)
  if (!identical(got, pgenlibr_sha)) {
    stop(sprintf("%s sha256 mismatch\n  expected %s\n  got      %s",
                 tarball, pgenlibr_sha, got))
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
work <- tempfile("vendorpgen-")
dir.create(work)
on.exit(unlink(work, recursive = TRUE), add = TRUE)
utils::untar(tb, exdir = work)
src <- file.path(work, "pgenlibr")

if (mode == "check") {
  out <- file.path(work, "out")
} else {
  out <- pkg_dst
}
dir.create(file.path(out, "src"), showWarnings = FALSE, recursive = TRUE)
dir.create(file.path(out, "tools", "include"), showWarnings = FALSE, recursive = TRUE)

message("copy package-root autoconf files (verbatim)")
for (f in root_files) {
  stopifnot(file.copy(file.path(src, f), file.path(out, f), overwrite = TRUE))
  Sys.chmod(file.path(out, f), "0755")
}

message("copy src/ files (verbatim)")
for (f in src_files_verbatim) {
  stopifnot(file.copy(file.path(src, "src", f), file.path(out, "src", f), overwrite = TRUE))
}

message("copy src/Makevars.in, src/Makevars.win (OBJECTS= edited for rpgen.cpp)")
for (f in src_files_patched) {
  lines <- readLines(file.path(src, "src", f))
  idx <- grep("^OBJECTS = ", lines)
  if (length(idx) != 1) {
    stop(f, ": expected exactly one 'OBJECTS = ' line, found ", length(idx))
  }
  old <- lines[idx]
  lines[idx] <- "OBJECTS = rpgen.o"
  message("  ", f, ": ", trimws(old), " -> ", lines[idx])
  writeLines(lines, file.path(out, "src", f))
}

message("copy tools/*_version.cpp compiler probes (verbatim)")
dir.create(file.path(out, "tools"), showWarnings = FALSE, recursive = TRUE)
for (f in tools_files) {
  stopifnot(file.copy(file.path(src, "tools", f), file.path(out, "tools", f), overwrite = TRUE))
}

message("copy tools/include/ (pgenlib core + zstd + libdeflate + simde, verbatim)")
inc_src <- file.path(src, "tools", "include")
inc_out <- file.path(out, "tools", "include")
unlink(inc_out, recursive = TRUE)
dir.create(inc_out, showWarnings = FALSE, recursive = TRUE)
inc_manifest <- list.files(inc_src, recursive = TRUE, all.files = FALSE, no.. = TRUE)
for (f in inc_manifest) {
  dir.create(file.path(inc_out, dirname(f)), showWarnings = FALSE, recursive = TRUE)
  stopifnot(file.copy(file.path(inc_src, f), file.path(inc_out, f), overwrite = TRUE))
}
message("  ", length(inc_manifest), " files")

message("copy inst/extdata/ test fixture (verbatim)")
dir.create(file.path(out, "inst", "extdata"), showWarnings = FALSE, recursive = TRUE)
for (f in extdata_files) {
  stopifnot(file.copy(file.path(src, "inst", "extdata", f),
                       file.path(out, "inst", "extdata", f), overwrite = TRUE))
}

if (mode == "check") {
  mism <- character(0)

  check_file <- function(rel) {
    a <- file.path(out, rel); b <- file.path(pkg_dst, rel)
    if (!file.exists(b) || !identical(sha256(a), sha256(b))) mism <<- c(mism, rel)
  }
  for (f in root_files) check_file(f)
  for (f in c(src_files_verbatim, src_files_patched)) check_file(file.path("src", f))
  for (f in tools_files) check_file(file.path("tools", f))
  for (f in inc_manifest) check_file(file.path("tools", "include", f))
  for (f in extdata_files) check_file(file.path("inst", "extdata", f))

  if (length(mism) == 0) {
    message("OK: committed vendored tree == vendorpgen.R output (",
            3 + length(src_files_verbatim) + length(src_files_patched) +
              length(tools_files) + length(inc_manifest) + length(extdata_files),
            " files)")
  } else {
    stop("MISMATCH - committed tree no longer equals the recipe:\n  ",
         paste(mism, collapse = "\n  "))
  }
} else {
  message("regenerated packages/Rpgen vendored subset from pgenlibr ", pgenlibr_ver)
}
