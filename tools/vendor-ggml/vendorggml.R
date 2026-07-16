#!/usr/bin/env Rscript
# Regenerate Rggml's vendored engine from one pinned ggml-org source tree.
# The committed tree is:
#
#     selected files from ggml-org/ggml v0.16.0
#   + patches/
#   + overlay/
#
# The archive transport is not trusted as the pin. GitHub may recompress tag
# archives, so the recipe hashes the selected extracted files and their paths.
#
# Usage (from anywhere):
#   Rscript tools/vendor-ggml/vendorggml.R download
#   Rscript tools/vendor-ggml/vendorggml.R vendor
#   Rscript tools/vendor-ggml/vendorggml.R check

args <- commandArgs(trailingOnly = TRUE)
mode <- if (length(args) > 0L) args[[1L]] else "check"

ggml_tag <- "v0.16.0"
ggml_commit <- "524f974bb21a1013408f76d71c15732482c0c3fe"
ggml_url <- sprintf(
  "https://github.com/ggml-org/ggml/archive/refs/tags/%s.tar.gz",
  ggml_tag
)
source_tree_sha <- "b721fe02a203abef32a6fc139aaf9d24ffc8150ef99076ede75b806a5f84a199"

get_script_path <- function() {
  cmd_args <- commandArgs()
  match <- regexpr("(?<=^--file=).+", cmd_args, perl = TRUE)
  script_dir <- dirname(regmatches(cmd_args, match))
  if (length(script_dir) != 1L) {
    stop("can't determine script directory: call this with Rscript")
  }
  normalizePath(script_dir)
}

here <- get_script_path()
repo <- normalizePath(file.path(here, "..", ".."))
ggml_dst <- file.path(repo, "packages", "Rggml", "inst", "ggml")
inc_dst <- file.path(repo, "packages", "Rggml", "inst", "include")
cache <- file.path(here, "cache")
archive <- file.path(cache, sprintf("ggml-%s.tar.gz", ggml_tag))

manifest <- readLines(file.path(here, "manifest.txt"))
manifest <- manifest[nzchar(manifest)]
public_headers <- c(
  "ggml.h", "ggml-alloc.h", "ggml-backend.h", "ggml-blas.h",
  "ggml-cpp.h", "ggml-cpu.h", "ggml-cuda.h", "ggml-vulkan.h", "gguf.h"
)
overlay_ggml <- c(
  "Makefile", "cblas.h", "rggml_cblas.c", "rggml_compat.h", "rggml_io.c"
)

sha256 <- function(path) unname(tools::sha256sum(path))

tree_sha256 <- function(root, rel) {
  rel <- sort(unique(rel), method = "radix")
  files <- file.path(root, rel)
  if (!all(file.exists(files))) {
    stop("pinned GGML source is missing: ", rel[which(!file.exists(files))[1L]])
  }
  record <- tempfile("ggml-tree-")
  on.exit(unlink(record), add = TRUE)
  writeLines(sprintf("%s  %s", sha256(files), rel), record, useBytes = TRUE)
  sha256(record)
}

selected_source_files <- function(root) {
  c(
    file.path("src", manifest),
    "src/ggml-blas/ggml-blas.cpp",
    file.path(
      "src/ggml-cuda",
      list.files(
        file.path(root, "src", "ggml-cuda"), recursive = TRUE,
        all.files = TRUE, no.. = TRUE, include.dirs = FALSE
      )
    ),
    file.path(
      "src/ggml-vulkan",
      list.files(
        file.path(root, "src", "ggml-vulkan"), recursive = TRUE,
        all.files = TRUE, no.. = TRUE, include.dirs = FALSE
      )
    ),
    file.path("include", public_headers)
  )
}

fetch_upstream <- function() {
  dir.create(cache, showWarnings = FALSE, recursive = TRUE)
  if (!file.exists(archive)) {
    download.file(ggml_url, archive, quiet = TRUE, mode = "wb")
  }
  archive
}

if (mode == "download") {
  fetch_upstream()
  quit(save = "no")
}
if (!(mode %in% c("vendor", "check"))) {
  stop("unknown mode: ", mode, ". Use download, vendor, or check")
}

work <- tempfile("vendorggml-")
dir.create(work)
on.exit(unlink(work, recursive = TRUE), add = TRUE)
utils::untar(fetch_upstream(), exdir = work)
src_root <- list.files(work, full.names = TRUE)[1L]

selected <- selected_source_files(src_root)
got <- tree_sha256(src_root, selected)
if (!identical(got, source_tree_sha)) {
  stop(
    sprintf(
      paste0(
        "selected GGML source tree sha256 mismatch\n",
        "  expected %s\n  got      %s"
      ),
      source_tree_sha, got
    )
  )
}
message(
  "source OK: ggml-org/ggml ", ggml_tag, " (",
  substr(ggml_commit, 1L, 12L), ", ", length(selected),
  " files, tree sha256 ", substr(got, 1L, 12L), "...)"
)

if (mode == "check") {
  ggml_out <- file.path(work, "out", "ggml")
  inc_out <- file.path(work, "out", "include")
} else {
  ggml_out <- ggml_dst
  inc_out <- inc_dst
  unlink(ggml_out, recursive = TRUE)
  unlink(file.path(inc_out, public_headers))
}
dir.create(ggml_out, showWarnings = FALSE, recursive = TRUE)
dir.create(inc_out, showWarnings = FALSE, recursive = TRUE)

copy_one <- function(from, to) {
  dir.create(dirname(to), showWarnings = FALSE, recursive = TRUE)
  if (!file.copy(from, to, overwrite = TRUE, copy.mode = TRUE)) {
    stop("could not copy ", from, " to ", to)
  }
}

message("copy official core and CPU source manifest")
for (path in manifest) {
  copy_one(file.path(src_root, "src", path), file.path(ggml_out, path))
}
copy_one(
  file.path(src_root, "src", "ggml-blas", "ggml-blas.cpp"),
  file.path(ggml_out, "ggml-blas.cpp")
)

copy_tree <- function(source, target) {
  files <- list.files(
    source, recursive = TRUE, all.files = TRUE, no.. = TRUE,
    include.dirs = FALSE
  )
  unlink(target, recursive = TRUE)
  for (path in files) {
    copy_one(file.path(source, path), file.path(target, path))
  }
  length(files)
}

n_cuda <- copy_tree(
  file.path(src_root, "src", "ggml-cuda"),
  file.path(ggml_out, "ggml-cuda")
)
n_vulkan <- copy_tree(
  file.path(src_root, "src", "ggml-vulkan"),
  file.path(ggml_out, "ggml-vulkan")
)
message("copy official CUDA (", n_cuda, " files) and Vulkan (", n_vulkan, " files)")

message("copy official public headers")
for (header in public_headers) {
  copy_one(
    file.path(src_root, "include", header),
    file.path(inc_out, header)
  )
}

message("apply Rggml patches")
patches <- sort(list.files(file.path(here, "patches"), "\\.patch$", full.names = TRUE))
for (patch_file in patches) {
  rc <- system2(
    "patch",
    c("-p1", "-s", "--no-backup-if-mismatch", "-d", shQuote(ggml_out)),
    stdin = patch_file, stdout = "", stderr = ""
  )
  if (rc != 0L) stop("patch failed: ", basename(patch_file))
  message("  ", basename(patch_file))
}

header_patches <- sort(list.files(
  file.path(here, "header-patches"), "\\.patch$", full.names = TRUE
))
for (patch_file in header_patches) {
  rc <- system2(
    "patch",
    c("-p1", "-s", "--no-backup-if-mismatch", "-d", shQuote(inc_out)),
    stdin = patch_file, stdout = "", stderr = ""
  )
  if (rc != 0L) stop("header patch failed: ", basename(patch_file))
  message("  include/", basename(patch_file))
}

message("overlay Rggml build and R-safe integration files")
for (path in overlay_ggml) {
  copy_one(file.path(here, "overlay", path), file.path(ggml_out, path))
}

if (mode == "check") {
  source_files <- function(root) {
    rel <- list.files(root, recursive = TRUE, all.files = TRUE, no.. = TRUE)
    rel <- rel[!grepl("(^|/)(generated)(/|$)", rel)]
    rel <- rel[!grepl("\\.(o|obj|a|d)$", rel)]
    rel <- rel[!grepl("(^|/)vulkan-shaders-gen(\\.exe)?$", rel)]
    sort(rel, method = "radix")
  }

  expected <- source_files(ggml_out)
  actual <- source_files(ggml_dst)
  missing <- setdiff(expected, actual)
  extra <- setdiff(actual, expected)
  common <- intersect(expected, actual)
  changed <- common[
    sha256(file.path(ggml_out, common)) != sha256(file.path(ggml_dst, common))
  ]

  header_changed <- public_headers[
    sha256(file.path(inc_out, public_headers)) !=
      sha256(file.path(inc_dst, public_headers))
  ]
  mismatch <- c(
    if (length(missing)) paste0("missing: ", missing),
    if (length(extra)) paste0("extra: ", extra),
    changed,
    if (length(header_changed)) file.path("include", header_changed)
  )
  if (length(mismatch) != 0L) {
    stop(
      "MISMATCH: committed vendored tree differs from the recipe:\n  ",
      paste(mismatch, collapse = "\n  ")
    )
  }
  message("OK: committed vendored tree equals the official-source recipe")
} else {
  message(
    "regenerated inst/ggml and inst/include from ggml-org/ggml ",
    ggml_tag, " + patches + overlay"
  )
}
