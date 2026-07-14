## Round-trip every PLINK 2 importer Rpgen exposes, then drive the same source
## through rpgen_ingest(). Text fixtures and the committed BGEN/BCF fixtures
## were generated or cross-checked with PLINK 2 v2.0.0-a.6.9. GEN, HAPS, and
## both BGEN layouts share one known genotype matrix. HAPS additionally pins
## exact phase, and legacy dosage pins a fractional value in compressed and
## full-precision destinations.

library(Rpgen)

if (!exists("rpgen_import_gen", where = asNamespace("Rpgen"), inherits = FALSE)) {
    exit_file("PLINK 2 importers not available in this build of Rpgen")
}

## Cleans up a .pgen's companion .pvar/.pvar.zst/.psam/.pgi files, same set
## test_import_vcf.R's own on.exit() blocks remove.
cleanup_pgen <- function(pgen) {
    pvar_plain <- sub("\\.pgen$", ".pvar", pgen)
    unlink(c(
        pgen, pvar_plain, paste0(pvar_plain, ".zst"),
        sub("\\.pgen$", ".psam", pgen), paste0(pgen, ".pgi")
    ))
}

with_ingest_runtime <- function(code) {
    tmp <- tempfile(fileext = ".bin")
    rt <- Rfmalloc::open_fmalloc(tmp, mode = "scratch", size_gb = 0.1)
    on.exit({
        Rfmalloc::cleanup_fmalloc(rt)
        unlink(tmp)
    }, add = TRUE)
    code(rt)
}

## -- shared base fixture: 3 samples x 4 biallelic SNPs, no missing calls ----
##
## Used by the .gen, .haps/.legend, and .bgen sections below - all four
## derived (via the plink2 oracle described in this file's header comment)
## from the *same* underlying genotypes, so they share one expected-hardcall
## matrix. Missing-call handling is exercised separately by the BCF section
## (reusing tiny.vcf's genotypes, which do include one) and by
## the PLINK 1 dosage section (a fractional, deliberately ambiguous dosage).
base_expected_hc <- matrix(
    c(
        0L, 1L, 2L, # rs1
        1L, 2L, 0L, # rs2
        0L, 0L, 2L, # rs3
        2L, 1L, 0L # rs4
    ),
    nrow = 3L, ncol = 4L
)

## PED/TPED do not carry an explicit REF/ALT declaration. PLINK 2 makes the
## major allele provisional REF, so rs4 avoids a tied allele count. This
## keeps the expected orientation deterministic while still exercising all
## three hardcall values.
legacy_expected_hc <- matrix(
    c(
        0L, 1L, 2L,
        1L, 2L, 0L,
        0L, 0L, 2L,
        2L, 0L, 0L
    ),
    nrow = 3L, ncol = 4L
)

legacy_allele_pair <- function(alt_count) {
    c("A A", "A G", "G G")[[alt_count + 1L]]
}

## -- rpgen_import_gen(): Oxford .gen + .sample, full round-trip ------------

(function() {
    message("Testing rpgen_import_gen() -> rpgen_read_hardcalls() round-trip...")

    ## Original plink2 5-column .gen layout: chr id pos a1 a2 <3 probs>*N.
    ## Each genotype triple is one of the "fast-path" exact values plink2's
    ## own importer special-cases ("1 0 0" / "0 1 0" / "0 0 1"): no
    ## intermediate rounding ambiguity, so the resulting hardcalls are exact.
    gen_lines <- c(
        "1 rs1 1000 G A 0 0 1 0 1 0 1 0 0",
        "1 rs2 2000 T C 0 1 0 1 0 0 0 0 1",
        "1 rs3 3000 A G 0 0 1 0 0 1 1 0 0",
        "1 rs4 4000 C T 1 0 0 0 1 0 0 0 1"
    )
    sample_lines <- c(
        "ID_1 ID_2 missing sex",
        "0 0 0 D",
        "0 SAMP1 0 NA",
        "0 SAMP2 0 NA",
        "0 SAMP3 0 NA"
    )
    gen_path <- tempfile(fileext = ".gen")
    sample_path <- tempfile(fileext = ".sample")
    writeLines(gen_lines, gen_path)
    writeLines(sample_lines, sample_path)
    on.exit(unlink(c(gen_path, sample_path)), add = TRUE)

    pgen_path <- tempfile(fileext = ".pgen")
    import_result <- tryCatch(
        rpgen_import_gen(gen_path, sample_path, pgen_path),
        error = function(e) e
    )
    if (inherits(import_result, "error")) {
        exit_file(paste(
            "rpgen_import_gen() failed in this environment:",
            conditionMessage(import_result)
        ))
    }
    on.exit(cleanup_pgen(pgen_path), add = TRUE)

    expect_identical(import_result, pgen_path)
    info <- rpgen_info(pgen_path)
    expect_equal(info$n_sample, 3L)
    expect_equal(info$n_variant, 4L)

    hc <- rpgen_read_hardcalls(pgen_path)
    expect_true(is.matrix(hc) && is.integer(hc))
    expect_equal(dim(hc), c(3L, 4L))
    expect_equal(hc, base_expected_hc,
        info = "rpgen_import_gen() genotypes match the .gen fixture's known genotype triples"
    )
    with_ingest_runtime(function(rt) {
        tn <- rpgen_ingest(
            gen_path, format = "gen", sample = sample_path, runtime = rt
        )
        expect_equal(Rfmalloc::fmalloc_tensor_materialize(tn)[],
                     base_expected_hc)
    })
})()

## -- rpgen_import_ped(): PLINK 1 sample-major PED/MAP ----------------------

(function() {
    message("Testing rpgen_import_ped() -> rpgen_read_hardcalls() round-trip...")

    map_path <- tempfile(fileext = ".map")
    ped_path <- tempfile(fileext = ".ped")
    writeLines(sprintf("1 rs%d 0 %d", 1:4, 1000L * (1:4)), map_path)
    writeLines(vapply(1:3, function(sample_idx) {
        pairs <- vapply(
            legacy_expected_hc[sample_idx, ], legacy_allele_pair, ""
        )
        paste(
            "FAM1", paste0("S", sample_idx), "0 0 0 -9",
            paste(pairs, collapse = " ")
        )
    }, ""), ped_path)
    on.exit(unlink(c(map_path, ped_path)), add = TRUE)

    pgen_path <- tempfile(fileext = ".pgen")
    import_result <- rpgen_import_ped(ped_path, map_path, pgen_path)
    on.exit(cleanup_pgen(pgen_path), add = TRUE)

    expect_identical(import_result, pgen_path)
    expect_equal(rpgen_info(pgen_path)[c("n_sample", "n_variant")],
                 list(n_sample = 3L, n_variant = 4L))
    expect_equal(rpgen_read_hardcalls(pgen_path), legacy_expected_hc)
    with_ingest_runtime(function(rt) {
        tn <- rpgen_ingest(ped_path, map = map_path, runtime = rt)
        expect_equal(Rfmalloc::fmalloc_tensor_materialize(tn)[],
                     legacy_expected_hc)
    })
})()

## -- rpgen_import_tped(): PLINK 1 variant-major TPED/TFAM ------------------

(function() {
    message("Testing rpgen_import_tped() -> rpgen_read_hardcalls() round-trip...")

    tfam_path <- tempfile(fileext = ".tfam")
    tped_path <- tempfile(fileext = ".tped")
    writeLines(sprintf("FAM1 S%d 0 0 0 -9", 1:3), tfam_path)
    writeLines(vapply(1:4, function(variant_idx) {
        pairs <- vapply(
            legacy_expected_hc[, variant_idx], legacy_allele_pair, ""
        )
        paste(
            "1", paste0("rs", variant_idx), "0", 1000L * variant_idx,
            paste(pairs, collapse = " ")
        )
    }, ""), tped_path)
    on.exit(unlink(c(tfam_path, tped_path)), add = TRUE)

    pgen_path <- tempfile(fileext = ".pgen")
    import_result <- rpgen_import_tped(tped_path, tfam_path, pgen_path)
    on.exit(cleanup_pgen(pgen_path), add = TRUE)

    expect_identical(import_result, pgen_path)
    expect_equal(rpgen_read_hardcalls(pgen_path), legacy_expected_hc)
    with_ingest_runtime(function(rt) {
        tn <- rpgen_ingest(tped_path, tfam = tfam_path, runtime = rt)
        expect_equal(Rfmalloc::fmalloc_tensor_materialize(tn)[],
                     legacy_expected_hc)
    })
})()

## -- rpgen_import_eigenstrat(): PACKEDANCESTRYMAP/.ind/.snp ---------------

(function() {
    message("Testing rpgen_import_eigenstrat() -> rpgen_read_hardcalls() round-trip...")

    ind_path <- tempfile(fileext = ".ind")
    snp_path <- tempfile(fileext = ".snp")
    geno_path <- tempfile(fileext = ".geno")
    writeLines(c(
        "SAMP1 U Control",
        "SAMP2 U Case",
        "SAMP3 U Ignore"
    ), ind_path)
    writeLines(
        sprintf("rs%d 1 0 %d A G", 1:4, 1000L * (1:4)),
        snp_path
    )

    ## PACKEDANCESTRYMAP has one 48-byte header followed by one 48-byte
    ## record per variant for this small sample count. The two hexadecimal
    ## values are PLINK 2/EIGENSOFT UpdateEighash() results for SAMP1..3 and
    ## rs1..4. Each genotype is a 2-bit reference-allele count, first sample
    ## in the high bits; 3 denotes missing. This pins the binary reader, not
    ## just the text metadata conversion.
    header_text <- charToRaw("GENO 3 4 a88090eb 12bfbe20")
    header <- c(
        header_text, as.raw(0), raw(48L - length(header_text) - 1L)
    )
    packed <- as.raw(apply(base_expected_hc, 2, function(alt_count) {
        sum(bitwShiftL(2L - alt_count, c(6L, 4L, 2L)))
    }))
    con <- file(geno_path, open = "wb")
    writeBin(header, con)
    for (byte in packed) {
        writeBin(c(byte, raw(47L)), con)
    }
    close(con)
    on.exit(unlink(c(ind_path, snp_path, geno_path)), add = TRUE)

    pgen_path <- tempfile(fileext = ".pgen")
    import_result <- rpgen_import_eigenstrat(
        geno_path, ind_path, snp_path, pgen_path
    )
    on.exit(cleanup_pgen(pgen_path), add = TRUE)

    expect_identical(import_result, pgen_path)
    expect_equal(rpgen_read_hardcalls(pgen_path), base_expected_hc)
    with_ingest_runtime(function(rt) {
        tn <- rpgen_ingest(
            geno_path, ind = ind_path, snp = snp_path, runtime = rt
        )
        expect_equal(Rfmalloc::fmalloc_tensor_materialize(tn)[],
                     base_expected_hc)
    })
})()

## -- rpgen_import_haps(): Oxford .haps/.legend/.sample with exact phase ----

(function() {
    message("Testing rpgen_import_haps() -> rpgen_read_hardcalls() round-trip (hardcall level)...")

    ## Standard .haps layout: one row per variant (rs1..rs4, matching
    ## `legend_lines` below 1:1), 6 columns per row (2 phased haplotype
    ## alleles per sample x 3 samples, sample order matching `sample_lines`
    ## below). This exact fixture is the plink2 oracle's own
    ## `--export hapslegend` output for the same genotypes base_expected_hc
    ## describes below (see this file's header comment) - not hand-derived
    ## from the .haps spec, since the a0/a1-to-REF/ALT and
    ## haplotype-order-to-allele-count conventions are exactly the sort of
    ## detail worth cross-checking against a real implementation rather than
    ## re-deriving by hand.
    haps_lines <- c(
        "1 1 1 0 0 0", # rs1
        "1 0 0 0 1 1", # rs2
        "1 1 1 1 0 0", # rs3
        "0 0 1 0 1 1" # rs4
    )
    legend_lines <- c(
        "id position a0 a1",
        "rs1 1000 G A",
        "rs2 2000 T C",
        "rs3 3000 A G",
        "rs4 4000 C T"
    )
    sample_lines <- c(
        "ID_1 ID_2 missing sex",
        "0 0 0 D",
        "0 SAMP1 0 NA",
        "0 SAMP2 0 NA",
        "0 SAMP3 0 NA"
    )
    haps_path <- tempfile(fileext = ".haps")
    legend_path <- tempfile(fileext = ".legend")
    sample_path <- tempfile(fileext = ".sample")
    writeLines(haps_lines, haps_path)
    writeLines(legend_lines, legend_path)
    writeLines(sample_lines, sample_path)
    on.exit(unlink(c(haps_path, legend_path, sample_path)), add = TRUE)

    pgen_path <- tempfile(fileext = ".pgen")
    import_result <- tryCatch(
        rpgen_import_haps(haps_path, legend_path, sample_path, chr = "1", out = pgen_path),
        error = function(e) e
    )
    if (inherits(import_result, "error")) {
        exit_file(paste(
            "rpgen_import_haps() failed in this environment:",
            conditionMessage(import_result)
        ))
    }
    on.exit(cleanup_pgen(pgen_path), add = TRUE)

    expect_identical(import_result, pgen_path)
    info <- rpgen_info(pgen_path)
    expect_equal(info$n_sample, 3L)
    expect_equal(info$n_variant, 4L)

    hc <- rpgen_read_hardcalls(pgen_path)
    expect_true(is.matrix(hc) && is.integer(hc))
    expect_equal(dim(hc), c(3L, 4L))
    ## Same underlying genotypes as the .gen/.bgen sections (this .haps
    ## fixture is a phased encoding of the identical base fixture: each
    ## variant's two per-sample haplotype alleles sum to the same 0/1/2
    ## alternate-allele count the unphased .gen encodes).
    expect_equal(hc, base_expected_hc,
        info = "rpgen_import_haps() genotypes (allele counts, phase collapsed) match the .haps/.legend fixture"
    )

    tmp <- tempfile(fileext = ".bin")
    rt <- Rfmalloc::open_fmalloc(tmp, mode = "scratch", size_gb = 0.1)
    on.exit({
        Rfmalloc::cleanup_fmalloc(rt)
        unlink(tmp)
    }, add = TRUE)
    hap <- rpgen_haplotypes(pgen_path, runtime = rt, block_size = 2L)
    raw_haps <- matrix(
        as.integer(unlist(strsplit(haps_lines, " ", fixed = TRUE))),
        nrow = 4L, byrow = TRUE
    )
    # The fixture's legend has a0 in the eventual ALT column and a1 in REF.
    # PgrGetP() returns reference/non-reference bits, so its calls are the
    # complement of the raw a0/a1 HAPS bit convention here.
    expected_hap <- 1L - raw_haps
    expect_true(inherits(hap, "fmalloc_haplotypes"))
    expect_equal(dim(hap), c(4L, 6L))
    expect_identical(Rfmalloc::fmalloc_hap_materialize(hap, runtime = rt)[],
                     expected_hap)
    hap2 <- rpgen_ingest(
        haps_path, format = "haps", representation = "haplotype",
        legend = legend_path, sample = sample_path, chr = "1", runtime = rt,
        block_size = 2L
    )
    expect_identical(Rfmalloc::fmalloc_hap_materialize(hap2, runtime = rt)[],
                     expected_hap)
})()

## -- rpgen_import_plink1_dosage(): legacy PLINK 1 --import-dosage ----------

(function() {
    message("Testing rpgen_import_plink1_dosage() -> rpgen_read_hardcalls()/rpgen_read_dosages() round-trip...")

    ## "single" dosage format (auto-inferred: one value per sample, since
    ## the header/.fam imply 3 samples and each data row has exactly 3
    ## trailing values - see Plink1DosageToPgen()'s format_infer logic,
    ## cited in src/rpgen_import.cpp). No id-delim modifier is used, so the
    ## header carries one FID/IID *pair* of columns per sample (matching
    ## the .fam below), not a single combined column.
    dosage_lines <- c(
        "SNP A1 A2 FAM1 S1 FAM1 S2 FAM1 S3",
        "rs1 G A 0 1 2",
        "rs2 T C 1 2 0",
        "rs3 A G 2 1.5 0" # rs3/S2: deliberately ambiguous (exactly between het and hom-alt) -> NA hardcall, dosage 1.5 preserved
    )
    fam_lines <- c(
        "FAM1 S1 0 0 0 -9",
        "FAM1 S2 0 0 0 -9",
        "FAM1 S3 0 0 0 -9"
    )
    map_lines <- c(
        "1 rs1 0 1000",
        "1 rs2 0 2000",
        "1 rs3 0 3000"
    )
    dosage_path <- tempfile(fileext = ".dosage")
    fam_path <- tempfile(fileext = ".fam")
    map_path <- tempfile(fileext = ".map")
    writeLines(dosage_lines, dosage_path)
    writeLines(fam_lines, fam_path)
    writeLines(map_lines, map_path)
    on.exit(unlink(c(dosage_path, fam_path, map_path)), add = TRUE)

    pgen_path <- tempfile(fileext = ".pgen")
    import_result <- tryCatch(
        rpgen_import_plink1_dosage(dosage_path, fam_path, map_path, out = pgen_path),
        error = function(e) e
    )
    if (inherits(import_result, "error")) {
        exit_file(paste(
            "rpgen_import_plink1_dosage() failed in this environment:",
            conditionMessage(import_result)
        ))
    }
    on.exit(cleanup_pgen(pgen_path), add = TRUE)

    expect_identical(import_result, pgen_path)
    info <- rpgen_info(pgen_path)
    expect_equal(info$n_sample, 3L)
    expect_equal(info$n_variant, 3L)

    expected_hc <- matrix(
        c(
            0L, 1L, 2L, # rs1
            1L, 2L, 0L, # rs2
            2L, NA, 0L # rs3: S2's dosage (1.5) is too ambiguous for a hardcall
        ),
        nrow = 3L, ncol = 3L
    )
    hc <- rpgen_read_hardcalls(pgen_path)
    expect_true(is.matrix(hc) && is.integer(hc))
    expect_equal(dim(hc), c(3L, 3L))
    expect_true(all(hc == expected_hc | (is.na(hc) & is.na(expected_hc))),
        info = "rpgen_import_plink1_dosage() hardcalls match the dosage fixture's known values"
    )

    expected_dosage <- matrix(
        c(
            0, 1, 2,
            1, 2, 0,
            2, 1.5, 0
        ),
        nrow = 3L, ncol = 3L
    )
    ds <- rpgen_read_dosages(pgen_path)
    expect_true(is.matrix(ds) && is.double(ds))
    expect_equal(dim(ds), c(3L, 3L))
    expect_equal(ds, expected_dosage,
        tolerance = 1e-3,
        info = "rpgen_import_plink1_dosage() preserves the fractional dosage even though its hardcall is NA"
    )
    with_ingest_runtime(function(rt) {
        tn <- rpgen_ingest(
            dosage_path, format = "plink1_dosage", representation = "dosage",
            fam = fam_path, map = map_path, runtime = rt
        )
        expect_equal(Rfmalloc::fmalloc_tensor_materialize(tn)[],
                     expected_dosage, tolerance = 1 / 127)
        full <- rpgen_ingest(
            dosage_path, format = "plink1_dosage", representation = "f64",
            fam = fam_path, map = map_path, runtime = rt
        )
        expect_true(Rfmalloc::is_fmalloc_vector(full))
        expect_equal(full[], expected_dosage, tolerance = 1e-3)
    })
})()

## -- rpgen_import_bcf(): BCF, full round-trip against a committed fixture --
##
## inst/extdata/tiny.bcf is a real BCF (bcftools view -Ob of tiny.vcf)
## committed to the tree, so this runs
## unconditionally with no bcftools on PATH at test time - BCF is a first-
## class import, not a best-effort one. Those genotypes include one missing
## call, so this section also exercises missingness through the BCF path,
## unlike the .gen/.haps/.bgen fixtures above, which deliberately avoid it.

(function() {
    message("Testing rpgen_import_bcf() -> rpgen_read_hardcalls() round-trip (committed tiny.bcf)...")

    bcf_path <- system.file("extdata", "tiny.bcf", package = "Rpgen")
    if (!nzchar(bcf_path)) {
        exit_file("inst/extdata/tiny.bcf missing from this install of Rpgen")
    }

    ## The genotypes of tiny.vcf/tiny.bcf, same as test_import_vcf.R:
    ##   rs1 A/G: 0/0 0/1 1/1   rs2 C/T: 0/1 1/1 0/0
    ##   rs3 G/A: ./. 0/0 1/1   rs4 T/C: 1/1 0/1 0/0
    expected_hc <- matrix(
        c(
            0L, 1L, 2L,
            1L, 2L, 0L,
            NA, 0L, 2L,
            2L, 1L, 0L
        ),
        nrow = 3L, ncol = 4L
    )

    pgen_path <- tempfile(fileext = ".pgen")
    import_result <- tryCatch(
        rpgen_import_bcf(bcf_path, pgen_path),
        error = function(e) e
    )
    if (inherits(import_result, "error")) {
        exit_file(paste(
            "rpgen_import_bcf() failed in this environment:",
            conditionMessage(import_result)
        ))
    }
    on.exit(cleanup_pgen(pgen_path), add = TRUE)

    expect_identical(import_result, pgen_path)
    info <- rpgen_info(pgen_path)
    expect_equal(info$n_sample, 3L)
    expect_equal(info$n_variant, 4L)

    hc <- rpgen_read_hardcalls(pgen_path)
    expect_true(all(hc == expected_hc | (is.na(hc) & is.na(expected_hc))),
        info = "rpgen_import_bcf() genotypes match the same known genotypes as test_import_vcf.R's VCF fixture"
    )
    with_ingest_runtime(function(rt) {
        tn <- rpgen_ingest(bcf_path, runtime = rt)
        got <- Rfmalloc::fmalloc_tensor_materialize(tn)[]
        expect_true(all(got == expected_hc |
                        (is.na(got) & is.na(expected_hc))))
    })
})()

## -- rpgen_import_bgen(): BGEN v1.1 (external .sample) and v1.3 (embedded
## sample IDs, sample = NULL), full round-trip against committed fixtures --
##
## inst/extdata/tiny.bgen + tiny.sample and inst/extdata/tiny_selfid.bgen
## are real BGEN files produced by a real plink2 build from the same known
## genotypes as the .gen/.haps fixtures above (see this file's header
## comment) - not hand-crafted bytes. No fixture gap: both a
## companion-.sample BGEN and a self-identifying (sample = NULL) BGEN are
## exercised.

(function() {
    message("Testing rpgen_import_bgen() -> rpgen_read_hardcalls() round-trip (v1.1, external .sample)...")

    bgen_path <- system.file("extdata", "tiny.bgen", package = "Rpgen")
    sample_path <- system.file("extdata", "tiny.sample", package = "Rpgen")
    if (!nzchar(bgen_path) || !nzchar(sample_path)) {
        exit_file("inst/extdata/tiny.bgen or tiny.sample missing from this install of Rpgen")
    }

    pgen_path <- tempfile(fileext = ".pgen")
    import_result <- tryCatch(
        rpgen_import_bgen(bgen_path, sample = sample_path, out = pgen_path),
        error = function(e) e
    )
    if (inherits(import_result, "error")) {
        exit_file(paste(
            "rpgen_import_bgen() failed in this environment:",
            conditionMessage(import_result)
        ))
    }
    on.exit(cleanup_pgen(pgen_path), add = TRUE)

    expect_identical(import_result, pgen_path)
    hc <- rpgen_read_hardcalls(pgen_path)
    expect_equal(hc, base_expected_hc,
        info = "rpgen_import_bgen() (v1.1, external .sample) genotypes match the shared base fixture"
    )
    with_ingest_runtime(function(rt) {
        tn <- rpgen_ingest(bgen_path, sample = sample_path, runtime = rt)
        expect_equal(Rfmalloc::fmalloc_tensor_materialize(tn)[],
                     base_expected_hc)
    })
})()

(function() {
    message("Testing rpgen_import_bgen() -> rpgen_read_hardcalls() round-trip (v1.3, sample = NULL, embedded IDs)...")

    bgen_path <- system.file("extdata", "tiny_selfid.bgen", package = "Rpgen")
    if (!nzchar(bgen_path)) {
        exit_file("inst/extdata/tiny_selfid.bgen missing from this install of Rpgen")
    }

    pgen_path <- tempfile(fileext = ".pgen")
    import_result <- tryCatch(
        rpgen_import_bgen(bgen_path, sample = NULL, out = pgen_path),
        error = function(e) e
    )
    if (inherits(import_result, "error")) {
        exit_file(paste(
            "rpgen_import_bgen(sample = NULL) failed in this environment:",
            conditionMessage(import_result)
        ))
    }
    on.exit(cleanup_pgen(pgen_path), add = TRUE)

    hc <- rpgen_read_hardcalls(pgen_path)
    expect_equal(hc, base_expected_hc,
        info = "rpgen_import_bgen(sample = NULL) genotypes match the shared base fixture, using the BGEN's own embedded sample IDs"
    )
    with_ingest_runtime(function(rt) {
        tn <- rpgen_ingest(bgen_path, runtime = rt)
        expect_equal(Rfmalloc::fmalloc_tensor_materialize(tn)[],
                     base_expected_hc)
    })
})()

## -- error paths: bad/empty input must fail cleanly, never crash R --------
##
## Every driver in this file shares rpgen_import_vcf()'s arena/jmp_buf
## machinery (src/rpgen_import.cpp), so a representative failure per format
## is enough to exercise that shared path here; test_import_vcf.R already
## covers it thoroughly for VcfToPgen() itself.

expect_error(rpgen_import_bcf(tempfile(fileext = ".bcf"), tempfile(fileext = ".pgen")),
    info = "a nonexistent BCF path is a clean error, not a crash"
)
expect_error(rpgen_import_bgen(tempfile(fileext = ".bgen"), out = tempfile(fileext = ".pgen")),
    info = "a nonexistent BGEN path is a clean error, not a crash"
)
expect_error(rpgen_import_gen(tempfile(fileext = ".gen"), tempfile(fileext = ".sample")),
    info = "a nonexistent .gen path is a clean error, not a crash"
)
expect_error(
    rpgen_import_haps(
        tempfile(fileext = ".haps"), tempfile(fileext = ".legend"),
        tempfile(fileext = ".sample"),
        chr = "1"
    ),
    info = "nonexistent .haps/.legend/.sample paths are a clean error, not a crash"
)
expect_error(
    rpgen_import_plink1_dosage(
        tempfile(fileext = ".dosage"), tempfile(fileext = ".fam"), tempfile(fileext = ".map")
    ),
    info = "nonexistent dosage/.fam/.map paths are a clean error, not a crash"
)
expect_error(
    rpgen_import_ped(tempfile(fileext = ".ped"), tempfile(fileext = ".map")),
    info = "nonexistent .ped/.map paths are a clean error, not a crash"
)
expect_error(
    rpgen_import_tped(tempfile(fileext = ".tped"), tempfile(fileext = ".tfam")),
    info = "nonexistent .tped/.tfam paths are a clean error, not a crash"
)
expect_error(
    rpgen_import_eigenstrat(
        tempfile(fileext = ".geno"), tempfile(fileext = ".ind"),
        tempfile(fileext = ".snp")
    ),
    info = "nonexistent EIGENSTRAT paths are a clean error, not a crash"
)

## out must end in .pgen, same contract as rpgen_import_vcf()'s.
expect_error(rpgen_import_bcf(tempfile(fileext = ".bcf"), tempfile()),
    info = "out must end in .pgen (bcf)"
)
expect_error(rpgen_import_plink1_dosage(
    tempfile(fileext = ".dosage"), tempfile(fileext = ".fam"), tempfile(fileext = ".map"),
    out = tempfile()
), info = "out must end in .pgen (plink1_dosage)")
expect_error(
    rpgen_import_eigenstrat(
        tempfile(fileext = ".geno"), tempfile(fileext = ".ind"),
        tempfile(fileext = ".snp"), out = tempfile()
    ),
    info = "out must end in .pgen (eigenstrat)"
)

## The staged route removes both its public PGEN family and PED/MAP's private
## sample-major scratch files, including after an importer failure.
cleanup_probe <- tempfile(fileext = ".pgen")
cleanup_base <- sub("\\.pgen$", "", cleanup_probe)
cleanup_paths <- c(
    cleanup_probe, paste0(cleanup_base, ".pvar"),
    paste0(cleanup_base, ".pvar.zst"), paste0(cleanup_base, ".psam"),
    paste0(cleanup_probe, ".pgi"), paste0(cleanup_base, ".bed.smaj"),
    paste0(cleanup_base, ".fam.tmp")
)
invisible(file.create(cleanup_paths))
Rpgen:::.rpgen_cleanup_pgen(cleanup_probe)
expect_false(any(file.exists(cleanup_paths)),
    info = "staged import cleanup removes PGEN companions and PED scratch files"
)
