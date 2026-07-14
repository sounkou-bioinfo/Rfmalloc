#' Convert a VCF to a .pgen using plink2's own importer
#'
#' Converts `vcf` to a `.pgen` by calling plink2's own `VcfToPgen()` importer
#' (vendored by `tools/vendor-plink2-import/`, see its `PROVENANCE.md`) with
#' the defaults a plain `plink2 --vcf <vcf> --make-pgen` (no other flags)
#' would use - no argv parsing happens; every default matches what plink2.cc
#' itself computes for that combination (see `src/rpgen_import.cpp`'s
#' comments for exactly which). The design choice this embodies: reuse
#' plink2's own import code rather than maintain a second parser for each
#' format. The identical closure covers BCF, BGEN, and Oxford inputs.
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

## Each function below is a thin wrapper around one entry point in the same
## vendored plink2_import.cc closure. See src/rpgen_import.cpp for shared
## defaults and the format-specific driver notes.

#' Convert a BCF to a .pgen using plink2's own importer
#'
#' Converts `bcf` to a `.pgen` by calling plink2's own `BcfToPgen()` importer
#' - BCF's binary-sibling counterpart to [rpgen_import_vcf()]'s `VcfToPgen()`,
#' living in the same vendored closure - with the defaults a plain
#' `plink2 --bcf <bcf> --make-pgen` (no other flags) would use.
#'
#' The produced `.pgen` can be read back with any of Rpgen's existing `.pgen`
#' readers, exactly as [rpgen_import_vcf()]'s can.
#'
#' @param bcf Path to a BCF file.
#' @param out Path to the `.pgen` to produce; must end in `.pgen`. Defaults
#'   to a fresh [tempfile()].
#' @return `out`, invisibly on success; an R error is raised on failure.
#' @seealso [rpgen_import_vcf()], [rpgen_read_hardcalls()]
#' @examples
#' vcf <- system.file("extdata", "tiny.vcf", package = "Rpgen")
#' if (nzchar(vcf) && nzchar(Sys.which("bcftools"))) {
#'     bcf <- tempfile(fileext = ".bcf")
#'     system2("bcftools", c("view", shQuote(vcf), "-Ob", "-o", shQuote(bcf)))
#'     pgen <- rpgen_import_bcf(bcf)
#'     rpgen_info(pgen)$n_sample
#'     unlink(c(bcf, pgen, sub("\\.pgen$", ".pvar", pgen), sub("\\.pgen$", ".psam", pgen)))
#' }
#' @export
rpgen_import_bcf <- function(bcf, out = tempfile(fileext = ".pgen")) {
    bcf <- path.expand(as.character(bcf))
    out <- path.expand(as.character(out))
    if (!grepl("\\.pgen$", out)) {
        stop("out must end in \".pgen\": \"", out, "\"")
    }
    invisible(.Call("RC_rpgen_import_bcf", bcf, out))
}

#' Convert an Oxford-format .gen + .sample to a .pgen using plink2's own importer
#'
#' Converts the Oxford-format `gen`/`sample` pair to a `.pgen` by calling
#' plink2's own `OxGenToPgen()` importer, with the defaults a plain
#' `plink2 --gen <gen> --sample <sample> --make-pgen` would use. Unlike
#' [rpgen_import_vcf()]'s VCF/BCF, a `.gen` file carries no sample IDs or
#' pedigree of its own, so `sample` is required, not optional.
#'
#' `gen`'s rows must carry an explicit leading chromosome column (plink2's
#' original 5-column `.gen` layout: `chr id pos a1 a2 <probabilities>...`);
#' this function does not expose an `--oxford-single-chr` equivalent.
#'
#' The produced `.pgen` can be read back with any of Rpgen's existing `.pgen`
#' readers, exactly as [rpgen_import_vcf()]'s can.
#'
#' @param gen Path to an Oxford-format `.gen` file (original 5-column
#'   layout, with a leading chromosome column).
#' @param sample Path to the companion `.sample` file. Required.
#' @param out Path to the `.pgen` to produce; must end in `.pgen`. Defaults
#'   to a fresh [tempfile()].
#' @return `out`, invisibly on success; an R error is raised on failure.
#' @seealso [rpgen_import_bgen()], [rpgen_import_haps()]
#' @export
rpgen_import_gen <- function(gen, sample, out = tempfile(fileext = ".pgen")) {
    gen <- path.expand(as.character(gen))
    sample <- path.expand(as.character(sample))
    out <- path.expand(as.character(out))
    if (!grepl("\\.pgen$", out)) {
        stop("out must end in \".pgen\": \"", out, "\"")
    }
    invisible(.Call("RC_rpgen_import_gen", gen, sample, out))
}

#' Convert a BGEN file to a .pgen using plink2's own importer
#'
#' Converts `bgen` (any of v1.1/v1.2/v1.3) to a `.pgen` by calling plink2's
#' own `OxBgenToPgen()` importer, with the defaults a plain
#' `plink2 --bgen <bgen> --sample <sample> --make-pgen` (or, if `sample` is
#' `NULL`, `plink2 --bgen <bgen> --make-pgen` alone) would use. Unlike
#' [rpgen_import_gen()]'s `.gen`, a BGEN v1.2/v1.3 file may carry its own
#' sample identifier block, so `sample` may be omitted when the file has one
#' - plink2's own importer raises a clear error if it does not.
#'
#' The produced `.pgen` can be read back with any of Rpgen's existing `.pgen`
#' readers, exactly as [rpgen_import_vcf()]'s can.
#'
#' @param bgen Path to a BGEN file (v1.1, v1.2, or v1.3).
#' @param sample Path to a companion `.sample` file, or `NULL` (the default)
#'   to use the BGEN's own embedded sample identifiers, if present.
#' @param out Path to the `.pgen` to produce; must end in `.pgen`. Defaults
#'   to a fresh [tempfile()].
#' @return `out`, invisibly on success; an R error is raised on failure.
#' @seealso [rpgen_import_gen()], [rpgen_import_haps()]
#' @examples
#' bgen <- system.file("extdata", "tiny.bgen", package = "Rpgen")
#' samp <- system.file("extdata", "tiny.sample", package = "Rpgen")
#' if (nzchar(bgen) && nzchar(samp)) {
#'     pgen <- rpgen_import_bgen(bgen, sample = samp)
#'     rpgen_info(pgen)$n_sample
#'     unlink(c(pgen, sub("\\.pgen$", ".pvar", pgen), sub("\\.pgen$", ".psam", pgen)))
#' }
#' @export
rpgen_import_bgen <- function(bgen, sample = NULL, out = tempfile(fileext = ".pgen")) {
    bgen <- path.expand(as.character(bgen))
    if (!is.null(sample)) {
        sample <- path.expand(as.character(sample))
    }
    out <- path.expand(as.character(out))
    if (!grepl("\\.pgen$", out)) {
        stop("out must end in \".pgen\": \"", out, "\"")
    }
    invisible(.Call("RC_rpgen_import_bgen", bgen, sample, out))
}

#' Convert Oxford-format phased haplotypes (.haps/.legend/.sample) to a .pgen
#'
#' Converts the Oxford-format `haps`/`legend`/`sample` triple to a `.pgen` by
#' calling plink2's own `OxHapslegendToPgen()` importer, with the defaults a
#' plain `plink2 --haps <haps> --legend <legend> <chr> --sample <sample>
#' --make-pgen` would use.
#'
#' A `.haps`/`.legend` pair encodes phased haplotypes and the produced `.pgen`
#' carries that phase. [rpgen_haplotypes()] and [rpgen_ingest()] recover it
#' through pgenlib's `PgrGetP()` path. The hardcall and dosage readers expose
#' the corresponding phase-collapsed non-reference count.
#'
#' `chr` is required: the classic IMPUTE2 `.legend` format (`id position a0
#' a1`, no chromosome column) does not carry its own chromosome, so plink2
#' itself requires one whenever `--legend` is used (confirmed via
#' `plink2 --help legend`).
#'
#' @param haps Path to a `.haps` file.
#' @param legend Path to the companion `.legend` file.
#' @param sample Path to the companion `.sample` file.
#' @param chr Chromosome code for every variant in `legend` (e.g. `"1"`).
#'   Required; see this function's Details.
#' @param out Path to the `.pgen` to produce; must end in `.pgen`. Defaults
#'   to a fresh [tempfile()].
#' @return `out`, invisibly on success; an R error is raised on failure.
#' @seealso [rpgen_import_gen()], [rpgen_import_bgen()], [rpgen_read_hardcalls()]
#' @export
rpgen_import_haps <- function(haps, legend, sample, chr, out = tempfile(fileext = ".pgen")) {
    haps <- path.expand(as.character(haps))
    legend <- path.expand(as.character(legend))
    sample <- path.expand(as.character(sample))
    chr <- as.character(chr)
    out <- path.expand(as.character(out))
    if (!grepl("\\.pgen$", out)) {
        stop("out must end in \".pgen\": \"", out, "\"")
    }
    invisible(.Call("RC_rpgen_import_haps", haps, legend, sample, chr, out))
}

#' Convert a legacy PLINK 1 --import-dosage file to a .pgen
#'
#' Converts the legacy PLINK 1.x `--import-dosage` text format (`dosage`,
#' plus companion `fam`/`map` files) to a `.pgen` by calling plink2's own
#' `Plink1DosageToPgen()` importer, with the defaults a plain
#' `plink2 --import-dosage <dosage> --fam <fam> --map <map> --make-pgen` (no
#' `--import-dosage` modifiers) would use - in particular, the per-sample
#' column format (a single dosage value, or a double/triple-probability
#' layout) is auto-inferred from `dosage`'s own column count, the same as
#' the plain command would do.
#'
#' The produced `.pgen` can be read back with any of Rpgen's existing `.pgen`
#' readers, exactly as [rpgen_import_vcf()]'s can.
#'
#' @param dosage Path to a PLINK 1 `--import-dosage`-format text file (a
#'   header line of space-separated `FID IID` pairs, then one row per
#'   variant of `ID A1 A2 <per-sample value(s)>`).
#' @param fam Path to the companion `.fam` file.
#' @param map Path to the companion `.map` file.
#' @param out Path to the `.pgen` to produce; must end in `.pgen`. Defaults
#'   to a fresh [tempfile()].
#' @return `out`, invisibly on success; an R error is raised on failure.
#' @seealso [rpgen_import_vcf()], [rpgen_read_dosages()]
#' @export
rpgen_import_plink1_dosage <- function(dosage, fam, map, out = tempfile(fileext = ".pgen")) {
    dosage <- path.expand(as.character(dosage))
    fam <- path.expand(as.character(fam))
    map <- path.expand(as.character(map))
    out <- path.expand(as.character(out))
    if (!grepl("\\.pgen$", out)) {
        stop("out must end in \".pgen\": \"", out, "\"")
    }
    invisible(.Call("RC_rpgen_import_plink1_dosage", dosage, fam, map, out))
}

#' Convert a PLINK 1 PED/MAP pair to a .pgen
#'
#' Calls PLINK 2's own `PedmapToPgen()` importer with the defaults of a plain
#' `plink2 --pedmap <prefix> --make-pgen` conversion. The resulting `.pgen`,
#' `.pvar`, and `.psam` files are ordinary PLINK 2 files and can be consumed
#' by every Rpgen reader.
#'
#' @param ped Path to the sample-major `.ped` file.
#' @param map Path to the companion `.map` file.
#' @param out Path to the `.pgen` to produce; must end in `.pgen`.
#' @return `out`, invisibly on success; an R error is raised on failure.
#' @seealso [rpgen_import_tped()], [rpgen_ingest()]
#' @export
rpgen_import_ped <- function(ped, map, out = tempfile(fileext = ".pgen")) {
    ped <- path.expand(as.character(ped))
    map <- path.expand(as.character(map))
    out <- path.expand(as.character(out))
    if (!grepl("\\.pgen$", out)) {
        stop("out must end in \".pgen\": \"", out, "\"")
    }
    invisible(.Call("RC_rpgen_import_ped", ped, map, out))
}

#' Convert a PLINK 1 TPED/TFAM pair to a .pgen
#'
#' Calls PLINK 2's own `TpedToPgen()` importer with the defaults of a plain
#' `plink2 --tfile <prefix> --make-pgen` conversion. The resulting `.pgen`,
#' `.pvar`, and `.psam` files are ordinary PLINK 2 files and can be consumed
#' by every Rpgen reader.
#'
#' @param tped Path to the variant-major `.tped` file.
#' @param tfam Path to the companion `.tfam` file.
#' @param out Path to the `.pgen` to produce; must end in `.pgen`.
#' @return `out`, invisibly on success; an R error is raised on failure.
#' @seealso [rpgen_import_ped()], [rpgen_ingest()]
#' @export
rpgen_import_tped <- function(tped, tfam, out = tempfile(fileext = ".pgen")) {
    tped <- path.expand(as.character(tped))
    tfam <- path.expand(as.character(tfam))
    out <- path.expand(as.character(out))
    if (!grepl("\\.pgen$", out)) {
        stop("out must end in \".pgen\": \"", out, "\"")
    }
    invisible(.Call("RC_rpgen_import_tped", tped, tfam, out))
}

#' Convert EIGENSOFT packed genotype data to a .pgen
#'
#' Calls PLINK 2's own `EigfileToPgen()` importer for an EIGENSOFT
#' PACKEDANCESTRYMAP or TGENO file and its `.ind`/`.snp` companions. The
#' importer validates the sample and variant hashes stored in the binary
#' header, matching a plain `plink2 --eigfile <prefix> --make-pgen`
#' conversion.
#'
#' @param geno Path to a PACKEDANCESTRYMAP `.geno` or TGENO binary file.
#' @param ind Path to the companion `.ind` file.
#' @param snp Path to the companion `.snp` file.
#' @param out Path to the `.pgen` to produce; must end in `.pgen`.
#' @return `out`, invisibly on success; an R error is raised on failure.
#' @seealso [rpgen_ingest()]
#' @export
rpgen_import_eigenstrat <- function(geno, ind, snp, out = tempfile(fileext = ".pgen")) {
    geno <- path.expand(as.character(geno))
    ind <- path.expand(as.character(ind))
    snp <- path.expand(as.character(snp))
    out <- path.expand(as.character(out))
    if (!grepl("\\.pgen$", out)) {
        stop("out must end in \".pgen\": \"", out, "\"")
    }
    invisible(.Call("RC_rpgen_import_eigenstrat", geno, ind, snp, out))
}
