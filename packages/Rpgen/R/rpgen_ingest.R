#' Ingest a PLINK2-supported genotype source into Rfmalloc storage
#'
#' `rpgen_ingest()` is the composition point for Rpgen's format matrix. It
#' accepts PGEN, PLINK1 BED, PED/MAP, TPED/TFAM, VCF/BCF, BGEN, Oxford GEN,
#' HAPS/legend, EIGENSTRAT, and legacy PLINK1 dosage sources, then writes one
#' of four compute-facing Rfmalloc
#' representations:
#'
#' * `"hardcall"`: 2-bit sample by variant genotypes;
#' * `"dosage"`: 1-byte fixed-point sample by variant dosages;
#' * `"haplotype"`: locus-major phased ref/non-ref bits;
#' * `"f64"`: uncompressed, full-precision dosage values.
#'
#' PGEN and BED are read by one persistent pgenlib reader into bounded record
#' panels. Other formats use PLINK2's own importer closure, with its terminal
#' `STPgenWriter` append redirected to the same Rfmalloc record sink. Their
#' decoded hardcall, dosage, and phase records therefore enter the selected
#' destination without a temporary PGEN serialization and read-back. PED/MAP
#' retains PLINK2's bounded sample-major transpose scratch file because that
#' source-to-destination layout change requires a transpose.
#'
#' Format is inferred from `path` when possible. Use `format` explicitly for
#' ambiguous legacy dosage paths. Companion arguments are used only by the
#' formats that require them.
#'
#' @param path Primary genotype path.
#' @param format One of `"pgen"`, `"bed"`, `"ped"`, `"tped"`, `"vcf"`,
#'   `"bcf"`, `"bgen"`, `"gen"`, `"haps"`, `"eigenstrat"`, or
#'   `"plink1_dosage"`. `NULL` infers it from the filename.
#' @param representation Destination representation: `"hardcall"`,
#'   `"dosage"`, `"haplotype"`, or `"f64"`.
#' @param runtime Runtime handle from [Rfmalloc::open_fmalloc()].
#' @param block_size Number of variants per transient PGEN or BED panel. `NULL`
#'   targets approximately 64 MiB. Native importers use their own bounded
#'   parser blocks and emit records directly.
#' @param sample Companion Oxford `.sample` path for BGEN, GEN, or HAPS. It is
#'   optional only when BGEN embeds sample identifiers.
#' @param pvar Optional PGEN `.pvar` path. The current collapsed ref/non-ref
#'   reads do not require it.
#' @param bim,fam Companion PLINK1 paths. BED infers `.bim` and `.fam` from
#'   `path` when omitted. Legacy dosage requires `fam` explicitly.
#' @param legend,chr Companion HAPS legend path and chromosome code.
#' @param map Companion PLINK1 `.map` path for PED or legacy dosage.
#' @param tfam Companion PLINK1 `.tfam` path for TPED.
#' @param ind,snp Companion EIGENSTRAT `.ind` and `.snp` paths.
#'
#' @return An `Rfmalloc::fmalloc_tensor` for hardcalls or compressed dosages,
#'   an `Rfmalloc::fmalloc_haplotypes` object for phased haplotypes, or an
#'   fmalloc-backed numeric matrix for `"f64"`.
#' @export
rpgen_ingest <- function(
    path,
    format = NULL,
    representation = c("hardcall", "dosage", "haplotype", "f64"),
    runtime = NULL,
    block_size = NULL,
    sample = NULL,
    pvar = NULL,
    bim = NULL,
    fam = NULL,
    legend = NULL,
    chr = NULL,
    map = NULL,
    tfam = NULL,
    ind = NULL,
    snp = NULL
) {
    path <- .rpgen_ingest_path(path, "path")
    representation <- match.arg(representation)
    format <- .rpgen_ingest_format(path, format)

    if (identical(format, "bed")) {
        if (identical(representation, "haplotype")) {
            stop("PLINK1 BED does not carry phase")
        }
        bim <- if (is.null(bim)) {
            .rpgen_bed_sibling(path, ".bim")
        } else {
            .rpgen_ingest_path(bim, "bim")
        }
        fam <- if (is.null(fam)) {
            .rpgen_bed_sibling(path, ".fam")
        } else {
            .rpgen_ingest_path(fam, "fam")
        }
        info <- list(
            n_sample = .rpgen_count_lines(fam),
            n_variant = .rpgen_count_lines(bim)
        )
        return(.rpgen_stream_fmalloc(
            path, info, .rpgen_representation_kind(representation),
            runtime, block_size
        ))
    }

    if (identical(format, "pgen")) {
        .rpgen_expand_pvar(pvar)
        return(.rpgen_stream_fmalloc(
            path, rpgen_info(path),
            .rpgen_representation_kind(representation), runtime, block_size
        ))
    }

    kind <- .rpgen_representation_kind(representation)
    runtime <- .rpgen_runtime(runtime)
    run_import <- switch(format,
        vcf = function(out) rpgen_import_vcf(path, out),
        bcf = function(out) rpgen_import_bcf(path, out),
        bgen = {
            sample <- if (is.null(sample)) NULL else
                .rpgen_ingest_path(sample, "sample")
            function(out) rpgen_import_bgen(path, sample = sample, out = out)
        },
        gen = {
            if (is.null(sample)) {
                stop("sample is required for Oxford GEN")
            }
            sample <- .rpgen_ingest_path(sample, "sample")
            function(out) rpgen_import_gen(path, sample, out)
        },
        haps = {
            if (is.null(legend) || is.null(sample) || is.null(chr)) {
                stop("legend, sample, and chr are required for HAPS")
            }
            legend <- .rpgen_ingest_path(legend, "legend")
            sample <- .rpgen_ingest_path(sample, "sample")
            chr <- as.character(chr)
            function(out) rpgen_import_haps(
                path, legend, sample, chr, out
            )
        },
        plink1_dosage = {
            if (is.null(fam) || is.null(map)) {
                stop("fam and map are required for PLINK1 dosage")
            }
            fam <- .rpgen_ingest_path(fam, "fam")
            map <- .rpgen_ingest_path(map, "map")
            function(out) rpgen_import_plink1_dosage(path, fam, map, out)
        },
        ped = {
            if (is.null(map)) {
                stop("map is required for PED")
            }
            map <- .rpgen_ingest_path(map, "map")
            function(out) rpgen_import_ped(path, map, out)
        },
        tped = {
            if (is.null(tfam)) {
                stop("tfam is required for TPED")
            }
            tfam <- .rpgen_ingest_path(tfam, "tfam")
            function(out) rpgen_import_tped(path, tfam, out)
        },
        eigenstrat = {
            if (is.null(ind) || is.null(snp)) {
                stop("ind and snp are required for EIGENSTRAT")
            }
            ind <- .rpgen_ingest_path(ind, "ind")
            snp <- .rpgen_ingest_path(snp, "snp")
            function(out) rpgen_import_eigenstrat(path, ind, snp, out)
        }
    )
    ## PLINK 2 derives metadata companion names from a .pgen-shaped outname.
    ## Direct mode never creates the genotype file at this path.
    import_path <- tempfile(fileext = ".pgen")
    on.exit(.rpgen_cleanup_pgen(import_path), add = TRUE)
    .Call(
        "RC_rpgen_direct_sink_begin",
        match(kind, c("bed", "dosage", "haplotype", "f64")) - 1L,
        runtime,
        if (identical(format, "haps")) .rpgen_count_lines(path) else 0
    )
    direct_active <- TRUE
    on.exit({
        if (direct_active) {
            .Call("RC_rpgen_direct_sink_abort")
        }
    }, add = TRUE)
    run_import(import_path)
    direct <- .Call("RC_rpgen_direct_sink_finish")
    direct_active <- FALSE
    .rpgen_wrap_fmalloc(
        direct$payload, kind, direct$n_sample, direct$n_variant
    )
}

.rpgen_ingest_path <- function(path, arg) {
    path <- as.character(path)
    if (length(path) != 1L || is.na(path) || !nzchar(path)) {
        stop(arg, " must be a single non-empty path")
    }
    path.expand(path)
}

.rpgen_representation_kind <- function(representation) {
    switch(representation,
        hardcall = "bed",
        dosage = "dosage",
        haplotype = "haplotype",
        f64 = "f64"
    )
}

.rpgen_cleanup_pgen <- function(path) {
    pvar <- sub("\\.pgen$", ".pvar", path)
    base <- sub("\\.pgen$", "", path)
    unlink(c(
        path, pvar, paste0(pvar, ".zst"),
        sub("\\.pgen$", ".psam", path), paste0(path, ".pgi"),
        paste0(base, ".bed.smaj"), paste0(base, ".fam.tmp")
    ))
}

.rpgen_ingest_format <- function(path, format) {
    choices <- c(
        "pgen", "bed", "ped", "tped", "vcf", "bcf", "bgen", "gen",
        "haps", "eigenstrat", "plink1_dosage"
    )
    if (!is.null(format)) {
        return(match.arg(format, choices))
    }
    lower <- tolower(path)
    detected <- if (grepl("\\.pgen$", lower)) {
        "pgen"
    } else if (grepl("\\.bed$", lower)) {
        "bed"
    } else if (grepl("\\.ped$", lower)) {
        "ped"
    } else if (grepl("\\.tped$", lower)) {
        "tped"
    } else if (grepl("\\.vcf(\\.gz|\\.bgz)?$", lower)) {
        "vcf"
    } else if (grepl("\\.bcf$", lower)) {
        "bcf"
    } else if (grepl("\\.bgen$", lower)) {
        "bgen"
    } else if (grepl("\\.gen(\\.gz)?$", lower)) {
        "gen"
    } else if (grepl("\\.haps?(\\.gz)?$", lower)) {
        "haps"
    } else if (grepl("\\.(geno|tgeno)$", lower)) {
        "eigenstrat"
    } else {
        NA_character_
    }
    if (is.na(detected)) {
        stop("cannot infer genotype format from path; supply format explicitly")
    }
    detected
}
