#!/usr/bin/env Rscript
# vendor pgenlib: reproducibly regenerate Rpgen's vendored PLINK2 pgenlib tree
# from a pinned, immutable source. Same shape as tools/vendor-ggml/vendorggml.R
# (self-locating, mode-driven, pins an exact upstream artifact), adapted for
# pgenlibr's autoconf-based build. This is how the project OWNS its path to
# pgenlib: the vendored tree is not a mystery copy, it is
#
#     (SHA-pinned pgenlibr CRAN tarball) - (Rcpp wrappers)
#                              + (Makevars and cleanup integration)
#
# recorded beside this script and verifiable by re-running it.
#
#   base  : pgenlibr <VER> source tarball from CRAN, SHA256-pinned. pgenlibr
#           is a CRAN-accepted package that already vendors exactly the
#           pgenlib *read* subset + zstd + libdeflate + simde and builds
#           libPLINK2.a from them; that build is our reference. We take its
#           configure/configure.ac/cleanup/Makevars* and its whole
#           tools/include/ tree as the base. We do NOT take pgenlibr's
#           pvar.cpp/pvar.h: they are
#           its Rcpp R-facing wrapper, and Rpgen is not an Rcpp package (it uses
#           the C FFI in include/pvar_ffi_support.*). R's Windows build compiles
#           every src/*.cpp regardless of OBJECTS, so shipping them broke the
#           Windows build on a missing Rcpp.h.
#   edit  : Makevars.in and Makevars.win each set one line,
#           "OBJECTS = pvar.o pgenlibr.o RcppExports.o", to the R-level Rcpp
#           bindings pgenlibr compiles. Rpgen has no Rcpp bindings - it has
#           its own hand-authored native files, not touched by this script, so
#           that line names rpgen.cpp, rpgen_import.cpp, the direct record
#           sink, and the two CLI cleanup shims. rpgen_import.cpp drives the
#           native format-import closure; see
#           tools/vendor-plink2-import/PROVENANCE.md).
#           Applied inline below rather than as a patches/*.patch file: it is
#           a single deterministic line replacement, not a source edit.
#   edit 2: Rpgen also adds PLINK 2's genotype format-import closure
#           (tools/vendor-plink2-import/vendorplink2import.R vendors the
#           source files themselves into tools/include/) to the same
#           libPLINK2.a static library this script's Makevars.in/Makevars.win
#           already build: pgenlib_write.cc joins the LIBPLINK2_SOURCES list,
#           the plink2_*.cc program-level files are appended to it, and a new
#           LIBPLINK2_C_SOURCES/LIBPLINK2_CO pair (SFMT.c is plain C) joins
#           the libPLINK2.a build rule. Applied inline below, right after the
#           OBJECTS= edit, for the same reason: deterministic, and it belongs
#           next to the file list it's wiring in. The same edit installs the
#           target-specific CLI/direct-sink flags used only for vendored
#           objects. A re-vendor therefore reproduces the complete build,
#           rather than silently dropping hand-maintained Makevars fragments.
#   edit 3: pgenlibr's cleanup script removes only objects directly under
#           src/. Rpgen also compiles the vendored tree in tools/include/, so
#           the generated cleanup script removes those external objects too.
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
# src/), except for the explicitly generated Makevars and cleanup edits above.
root_files_verbatim <- c("configure", "configure.ac")
root_files_patched <- "cleanup"
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
for (f in root_files_verbatim) {
  stopifnot(file.copy(file.path(src, f), file.path(out, f), overwrite = TRUE))
  Sys.chmod(file.path(out, f), "0755")
}

message("copy cleanup script (vendored-object cleanup added)")
cleanup_lines <- readLines(file.path(src, root_files_patched))
cleanup_lines <- c(
  cleanup_lines,
  "find tools/include -type f -name '*.o' -exec rm -f {} \\;"
)
writeLines(cleanup_lines, file.path(out, root_files_patched))
Sys.chmod(file.path(out, root_files_patched), "0755")

message("copy src/ files (verbatim)")
for (f in src_files_verbatim) {
  stopifnot(file.copy(file.path(src, "src", f), file.path(out, "src", f), overwrite = TRUE))
}

# "edit 2" (see this script's header comment): wires PLINK 2's genotype
# format-import closure (vendored separately by
# tools/vendor-plink2-import/vendorplink2import.R into tools/include/) into
# the same libPLINK2.a build these Makevars templates already produce.
# file_label picks the (slightly different) explanatory comment each file
# gets - Makevars.in's is the canonical one, Makevars.win's just points back
# to it.
patch_libplink2_sources <- function(lines, file_label) {
  idx <- grep("^LIBPLINK2_SOURCES = ", lines)
  if (length(idx) != 1) {
    stop(file_label, ": expected exactly one 'LIBPLINK2_SOURCES = ' line, found ", length(idx))
  }
  if (grepl("plink2_import.cc", lines[idx], fixed = TRUE)) {
    stop(file_label, ": LIBPLINK2_SOURCES already contains plink2_import.cc (double patch?)")
  }
  # pgenlib_write.cc (the .pgen writer, library-level) joins the pgenlib
  # read-subset entries already there; the program-level plink2_*.cc files
  # (no "include/" prefix - see tools/vendor-plink2-import/PROVENANCE.md)
  # are appended at the end.
  lines[idx] <- sub("\\$\\(INCL\\)/include/pgenlib_read\\.cc",
                     "$(INCL)/include/pgenlib_read.cc $(INCL)/include/pgenlib_write.cc",
                     lines[idx])
  extra_program_files <- paste(sprintf("$(INCL)/%s", c(
    "plink2_import.cc", "plink2_common.cc", "plink2_cmdline.cc",
    "plink2_pvar.cc", "plink2_psam.cc", "plink2_compress_stream.cc",
    "plink2_import_legacy.cc", "plink2_random.cc", "plink2_decompress.cc",
    "plink2_data.cc", "plink2_family.cc"
  )), collapse = " ")
  lines[idx] <- paste(lines[idx], extra_program_files)

  comment <- if (identical(file_label, "Makevars.in")) {
    c("# pgenlib read subset (vendorpgen.R) plus the PLINK 2 format-import",
      "# closure (vendorplink2import.R): pgenlib_write.cc is the",
      "# .pgen writer, the rest of the new entries are plink2's *program*-level",
      "# files (no \"include/\" path prefix - they live one directory up from the",
      "# library subset, see tools/vendor-plink2-import/PROVENANCE.md).")
  } else {
    c("# pgenlib read subset (vendorpgen.R) plus the PLINK 2 format-import",
      "# closure (vendorplink2import.R) - see src/Makevars.in's",
      "# comment on this same variable for the file-layout rationale.")
  }
  lines <- append(lines, comment, after = idx - 1)
  idx <- idx + length(comment)  # LIBPLINK2_SOURCES line shifted down by the inserted comment

  idx_obj <- grep("^LIBPLINK2 = \\$\\(LIBPLINK2_SOURCES:\\.cc=\\.o\\)$", lines)
  if (length(idx_obj) != 1) {
    stop(file_label, ": expected exactly one LIBPLINK2 object-list line, found ", length(idx_obj))
  }
  c_sources_comment <- if (identical(file_label, "Makevars.in")) {
    c("",
      "# SFMT is plain C (not C++), so it needs its own suffix rule; SFMT.h wraps",
      "# its declarations in extern \"C\" for C++ callers (plink2_random.cc), so the",
      "# two object kinds link together without a name-mangling mismatch.",
      "LIBPLINK2_C_SOURCES = $(INCL)/include/SFMT.c",
      "LIBPLINK2_CO = $(LIBPLINK2_C_SOURCES:.c=.o)")
  } else {
    c("",
      "# SFMT is plain C (not C++) - see src/Makevars.in's comment on this same",
      "# variable.",
      "LIBPLINK2_C_SOURCES = $(INCL)/include/SFMT.c",
      "LIBPLINK2_CO = $(LIBPLINK2_C_SOURCES:.c=.o)")
  }
  lines <- append(lines, c_sources_comment, after = idx_obj)

  idx_lib_a <- grep("^libPLINK2\\.a: \\$\\(LIBPLINK2\\)", lines)
  if (length(idx_lib_a) != 1) {
    stop(file_label, ": expected exactly one libPLINK2.a rule line, found ", length(idx_lib_a))
  }
  lines[idx_lib_a] <- sub("^libPLINK2\\.a: \\$\\(LIBPLINK2\\)",
                           "libPLINK2.a: $(LIBPLINK2) $(LIBPLINK2_CO)", lines[idx_lib_a])

  idx_ar <- grep("^\\s*\\$\\(AR\\) rcs libPLINK2\\.a \\$\\(LIBPLINK2\\)$", lines)
  if (length(idx_ar) != 1) {
    stop(file_label, ": expected exactly one libPLINK2.a AR recipe line, found ", length(idx_ar))
  }
  lines[idx_ar] <- paste0(lines[idx_ar], " $(LIBPLINK2_CO)")

  idx_clean <- grep("^\\s*rm -f .*\\$\\(LIBPLINK2\\)", lines)
  if (length(idx_clean) != 1) {
    stop(file_label, ": expected exactly one clean rm -f line mentioning LIBPLINK2, found ", length(idx_clean))
  }
  lines[idx_clean] <- sub(
    "\\$\\(LIBPLINK2\\)",
    "libPLINK2.a libPGZSTD.a libPGDEFLATE.a $(LIBPLINK2) $(LIBPLINK2_CO)",
    lines[idx_clean]
  )

  # Normalize any run of 2+ blank lines down to 1 (never semantically
  # meaningful in a Makefile): the insertions above add a blank line before
  # each comment block irrespective of how much blank space was already
  # there in the stock file, which otherwise leaves a doubled blank line in
  # Makevars.in specifically (it had two consecutive blank lines before this
  # patch; Makevars.win only ever had one).
  is_blank <- (lines == "")
  keep <- !(is_blank & c(FALSE, utils::head(is_blank, -1)))
  lines[keep]
}

# Keep Rpgen's hand-authored objects and the flags applying only to vendored
# PLINK 2 objects in the recipe. Re-vendoring must reproduce a buildable
# package, not a partial Makevars that requires manual repair afterward.
patch_rpgen_build <- function(lines, file_label) {
  idx_cpp <- grep("^PKG_CPPFLAGS = ", lines)
  if (length(idx_cpp) != 1) {
    stop(file_label, ": expected exactly one 'PKG_CPPFLAGS = ' line, found ",
         length(idx_cpp))
  }
  lines[idx_cpp] <- sub("^PKG_CPPFLAGS = ", "PKG_CPPFLAGS_BASE = ",
                        lines[idx_cpp])
  lines <- append(lines, "PKG_CPPFLAGS = $(PKG_CPPFLAGS_BASE)",
                  after = idx_cpp)

  idx_obj <- grep("^OBJECTS = ", lines)
  if (length(idx_obj) != 1) {
    stop(file_label, ": expected exactly one 'OBJECTS = ' line, found ",
         length(idx_obj))
  }
  lines[idx_obj] <- paste(
    "OBJECTS = rpgen.o rpgen_import.o rpgen_direct_sink.o",
    "rpgen_null_stream.o rpgen_plink2_glue.o"
  )

  c(lines, "",
    "# Only vendored objects receive the CLI shim and direct-writer hook.",
    "# Plain '=' avoids a GNU Make extension and a recursive variable.",
    "$(LIBPLINK2) $(LIBPLINK2_CO): PKG_CPPFLAGS = $(PKG_CPPFLAGS_BASE) -DRPGEN_DIRECT_SINK -I. -include rpgen_cli_shim.h")
}

message("copy src/Makevars.in, src/Makevars.win (Rpgen objects and vendored flags applied;")
message("  LIBPLINK2_SOURCES extended with the PLINK 2 format-import closure)")
for (f in src_files_patched) {
  lines <- readLines(file.path(src, "src", f))
  lines <- patch_libplink2_sources(lines, f)
  lines <- patch_rpgen_build(lines, f)
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
  for (f in root_files_verbatim) check_file(f)
  for (f in root_files_patched) check_file(f)
  for (f in src_files_verbatim) check_file(file.path("src", f))
  for (f in src_files_patched) check_file(file.path("src", f))
  for (f in tools_files) check_file(file.path("tools", f))
  for (f in inc_manifest) check_file(file.path("tools", "include", f))
  for (f in extdata_files) check_file(file.path("inst", "extdata", f))

  if (length(mism) == 0) {
    message("OK: committed vendored tree == vendorpgen.R output (",
            length(root_files_verbatim) + length(root_files_patched) +
              length(src_files_verbatim) +
              length(tools_files) + length(inc_manifest) + length(extdata_files),
            " files including generated Makevars integration)")
  } else {
    stop("MISMATCH - committed tree no longer equals the recipe:\n  ",
         paste(mism, collapse = "\n  "))
  }
} else {
  message("regenerated packages/Rpgen vendored subset from pgenlibr ", pgenlibr_ver)
}
