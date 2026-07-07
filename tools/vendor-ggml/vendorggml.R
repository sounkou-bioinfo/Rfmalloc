#!/usr/bin/env Rscript
# vendor GGML: reproducibly regenerate Rggml's vendored GGML tree from a
# pinned, immutable source. Same shape as Rtinycc's tools/vendortinycc.R
# (self-locating, mode-driven, pins an exact upstream artifact), adapted for
# GGML's extra steps. This is how the project OWNS its path to GGML: the
# vendored tree is not a mystery copy from a repository that rewrites its own
# history, it is
#
#     (SHA-pinned ggmlR CRAN tarball) + (patches/) + (overlay/)
#
# all recorded beside this script and verifiable by re-running it.
#
#   base    : ggmlR <VER> source tarball from CRAN, SHA256-pinned. 45 of the 52
#             vendored GGML files are byte-identical to it; ggmlR supplies the
#             GGML 0.9.5 sources already split for CRAN + the stdio/abort shim.
#   patches : patches/*.patch — our 7 local edits on top of stock ggmlR
#             (Windows/MinGW by-pointer buffer iface, never-destroyed teardown
#             singletons, NULL-guards against small-pool heap corruption, and
#             the arch-fallback.h hook that lets our runtime SIMD dispatcher
#             own the canonical q4_K vec_dot). Documented in PROVENANCE.md.
#   overlay : overlay/* — files that are OURS, not GGML's (the Makefile, the
#             BLAS backend + cblas->Fortran shim, the R-safe I/O shim).
#
# Usage (from anywhere):
#   Rscript tools/vendor-ggml/vendorggml.R download   # fetch + SHA-verify the base tarball into cache/
#   Rscript tools/vendor-ggml/vendorggml.R vendor     # regenerate inst/ggml + inst/include in place
#   Rscript tools/vendor-ggml/vendorggml.R check      # regenerate to a temp dir, diff vs the committed tree

args <- commandArgs(trailingOnly = TRUE)
mode <- if (length(args) > 0) args[[1]] else "check"

## --- pinned base ------------------------------------------------------------
ggmlr_ver <- "0.7.8"
ggmlr_sha <- "f7f414729e389dce7320cfcfd5c63298382da00c436e3e5bc49bf33f067d0dc7"
tarball   <- sprintf("ggmlR_%s.tar.gz", ggmlr_ver)
cran_urls <- c(
  sprintf("https://cran.r-project.org/src/contrib/%s", tarball),
  sprintf("https://cran.r-project.org/src/contrib/Archive/ggmlR/%s", tarball)
)

## --- locate ourselves (as Rtinycc's tools/vendortinycc.R does) --------------
getScriptPath <- function() {
  cmd.args <- commandArgs()
  m <- regexpr("(?<=^--file=).+", cmd.args, perl = TRUE)
  script.dir <- dirname(regmatches(cmd.args, m))
  if (length(script.dir) != 1) {
    stop("can't determine script dir: call this with Rscript")
  }
  normalizePath(script.dir)
}
here     <- getScriptPath()                                   # tools/vendor-ggml
repo     <- normalizePath(file.path(here, "..", ".."))        # monorepo root
ggml_dst <- file.path(repo, "packages", "Rggml", "inst", "ggml")
inc_dst  <- file.path(repo, "packages", "Rggml", "inst", "include")
cache    <- file.path(here, "cache")
inc_headers <- c("ggml.h", "ggml-alloc.h", "ggml-backend.h", "ggml-cpu.h")
overlay_ggml <- c("Makefile", "ggml-blas.cpp", "cblas.h",
                  "rggml_cblas.c", "rggml_compat.h", "rggml_io.c")

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
  if (!identical(got, ggmlr_sha)) {
    stop(sprintf("%s sha256 mismatch\n  expected %s\n  got      %s",
                 tarball, ggmlr_sha, got))
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

## --- regenerate -------------------------------------------------------------
tb   <- fetch_base()
work <- tempfile("vendorggml-")
dir.create(work)
on.exit(unlink(work, recursive = TRUE), add = TRUE)
utils::untar(tb, exdir = work)
src <- file.path(work, "ggmlR", "src")

if (mode == "check") {
  ggml_out <- file.path(work, "out", "ggml")
  inc_out  <- file.path(work, "out", "include")
} else {
  ggml_out <- ggml_dst
  inc_out  <- inc_dst
}
dir.create(file.path(ggml_out, "ggml-cpu"), showWarnings = FALSE, recursive = TRUE)
dir.create(inc_out, showWarnings = FALSE, recursive = TRUE)

message("copy vendored GGML subset (manifest.txt)")
manifest <- readLines(file.path(here, "manifest.txt"))
manifest <- manifest[nzchar(manifest)]
for (f in manifest) {
  dir.create(file.path(ggml_out, dirname(f)), showWarnings = FALSE, recursive = TRUE)
  stopifnot(file.copy(file.path(src, f), file.path(ggml_out, f), overwrite = TRUE))
}

message("copy installed public headers")
for (h in inc_headers) {
  stopifnot(file.copy(file.path(src, h), file.path(inc_out, h), overwrite = TRUE))
}

message("apply our patches (stock ", ggmlr_ver, " -> ours)")
for (p in sort(list.files(file.path(here, "patches"), "\\.patch$", full.names = TRUE))) {
  rc <- system2("patch", c("-p1", "-s", "-d", shQuote(ggml_out)),
                stdin = p, stdout = "", stderr = "")
  if (rc != 0) stop("patch failed: ", basename(p))
  message("  ", basename(p))
}

message("overlay our own (non-GGML) files")
for (f in overlay_ggml) {
  stopifnot(file.copy(file.path(here, "overlay", f), file.path(ggml_out, f), overwrite = TRUE))
}
stopifnot(file.copy(file.path(here, "overlay", "ggml-blas.h"),
                    file.path(inc_out, "ggml-blas.h"), overwrite = TRUE))

if (mode == "check") {
  ref_ggml <- ggml_dst; ref_inc <- inc_dst
  mism <- character(0)
  ours <- setdiff(list.files(ggml_out, recursive = TRUE),
                  c("libggml.a", list.files(ggml_out, "\\.o$", recursive = TRUE)))
  for (f in ours) {
    a <- file.path(ggml_out, f); b <- file.path(ref_ggml, f)
    if (!file.exists(b) || !identical(sha256(a), sha256(b))) mism <- c(mism, f)
  }
  for (h in c(inc_headers, "ggml-blas.h")) {
    if (!identical(sha256(file.path(inc_out, h)), sha256(file.path(ref_inc, h)))) {
      mism <- c(mism, file.path("include", h))
    }
  }
  if (length(mism) == 0) {
    message("OK: committed vendored tree == vendorggml.R output")
  } else {
    stop("MISMATCH — committed tree no longer equals the recipe:\n  ",
         paste(mism, collapse = "\n  "))
  }
} else {
  message("regenerated inst/ggml + inst/include from ggmlR ", ggmlr_ver,
          " + patches + overlay")
}
