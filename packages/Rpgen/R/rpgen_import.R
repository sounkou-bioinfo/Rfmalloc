#' Convert a VCF to a .pgen using plink2's own importer
#'
#' Converts `vcf` to a `.pgen` by calling plink2's own `VcfToPgen()` importer
#' (vendored by `tools/vendor-plink2-import/`, see its `PROVENANCE.md`) with
#' the defaults a plain `plink2 --vcf <vcf> --make-pgen` (no other flags)
#' would use - no argv parsing happens; every default matches what plink2.cc
#' itself computes for that combination (see `src/rpgen_import.cpp`'s
#' comments for exactly which). The design choice this embodies: reuse
#' plink2's own import code rather than a from-scratch `htslib`-based VCF
#' reader, because the identical vendored closure also covers BCF/BGEN/
#' Oxford for a later milestone.
#'
#' The produced `.pgen` (plus its companion `.pvar`/`.psam`, written next to
#' it with the same base name) can be read back with any of Rpgen's existing
#' `.pgen` readers - [rpgen_info()], [rpgen_read_hardcalls()],
#' [rpgen_read_dosages()], or [rpgen_bed()]/[rpgen_dosage()] for the
#' `Rfmalloc` tensor surface - since it is an ordinary `.pgen`, not a special
#' format of its own.
#'
#' @param vcf Path to a VCF file (may be gzip/bgzip-compressed; plink2's own
#'   importer detects that from the file's magic bytes, not its extension).
#' @param out Path to the `.pgen` to produce; must end in `.pgen`. Defaults
#'   to a fresh [tempfile()]. The companion `.pvar`/`.psam` are written next
#'   to it, with the same base name and their own conventional extensions.
#' @return `out`, invisibly on success (matching the input, since plink2's
#'   importer writes exactly there); an R error is raised on failure, with
#'   plink2's own diagnostic already relayed to the R console (see
#'   `src/rpgen_import.cpp`'s top comment for why the error path has two
#'   halves).
#' @seealso [rpgen_bed()], [rpgen_read_hardcalls()]
#' @examples
#' vcf <- system.file("extdata", "tiny.vcf", package = "Rpgen")
#' if (nzchar(vcf)) {
#'     pgen <- rpgen_import_vcf(vcf)
#'     info <- rpgen_info(pgen)
#'     info$n_sample
#'     unlink(c(pgen, sub("\\.pgen$", ".pvar", pgen), sub("\\.pgen$", ".psam", pgen)))
#' }
#' @export
rpgen_import_vcf <- function(vcf, out = tempfile(fileext = ".pgen")) {
    vcf <- path.expand(as.character(vcf))
    out <- path.expand(as.character(out))
    if (!grepl("\\.pgen$", out)) {
        stop("out must end in \".pgen\": \"", out, "\"")
    }
    invisible(.Call("RC_rpgen_import_vcf", vcf, out))
}

#' Convert a VCF straight into an Rfmalloc bed tensor
#'
#' Convenience wrapper chaining [rpgen_import_vcf()] and [rpgen_bed()]: the
#' VCF is imported to a (by default temporary) `.pgen` via plink2's own
#' importer, immediately read back through Rpgen's existing `.pgen` reader,
#' and packed into fmalloc-backed 2-bit `.bed` storage. The intermediate
#' `.pgen`/`.pvar`/`.psam` files are removed afterward unless `keep = TRUE`.
#'
#' @param vcf Path to a VCF file; see [rpgen_import_vcf()].
#' @param out Path to the intermediate `.pgen`; see [rpgen_import_vcf()].
#'   Defaults to a fresh [tempfile()].
#' @param keep Keep the intermediate `.pgen`/`.pvar`/`.psam` files instead of
#'   deleting them after the read. Defaults to `FALSE`.
#' @param runtime Runtime handle from [Rfmalloc::open_fmalloc()]; see
#'   [rpgen_bed()]. Defaults to the runtime established by
#'   [Rfmalloc::init_fmalloc()].
#' @return An `fmalloc_tensor` of dtype `"bed"`, `n_sample x n_variant`; see
#'   [rpgen_bed()].
#' @seealso [rpgen_import_vcf()], [rpgen_bed()]
#' @examples
#' vcf <- system.file("extdata", "tiny.vcf", package = "Rpgen")
#' if (nzchar(vcf)) {
#'     rt <- Rfmalloc::open_fmalloc(tempfile(), size_gb = 0.5)
#'     tn <- rpgen_import_bed(vcf, runtime = rt)
#'     dim(tn)
#'     Rfmalloc::cleanup_fmalloc(rt)
#' }
#' @export
rpgen_import_bed <- function(vcf, out = tempfile(fileext = ".pgen"), keep = FALSE, runtime = NULL) {
    pgen <- rpgen_import_vcf(vcf, out = out)
    if (!keep) {
        on.exit({
            pvar_plain <- sub("\\.pgen$", ".pvar", pgen)
            unlink(c(
                pgen, pvar_plain, paste0(pvar_plain, ".zst"),
                sub("\\.pgen$", ".psam", pgen), paste0(pgen, ".pgi")
            ))
        }, add = TRUE)
    }
    rpgen_bed(pgen, runtime = runtime)
}
