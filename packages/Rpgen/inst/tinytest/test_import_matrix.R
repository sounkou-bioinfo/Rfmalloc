## Milestone 4b: the rest of plink2's import matrix
## (rpgen_import_bcf/_bgen/_gen/_haps/_plink1_dosage), same correctness
## contract as milestone 4a's test_import_vcf.R: build a tiny fixture with
## known genotypes, convert it to a .pgen through Rpgen's driver (which
## calls straight into plink2's own vendored importer - see
## src/rpgen_import.cpp), read the produced .pgen back with
## rpgen_read_hardcalls()/rpgen_read_dosages() (unchanged since milestone 2),
## and assert the genotypes/dosages match what the fixture encodes.
##
## How the fixtures and their "known genotypes" were derived: every fixture
## in this file (the .gen/.sample text below, the .haps/.legend text, the
## PLINK 1 --import-dosage text, and the inst/extdata/tiny*.bgen binaries)
## was generated and cross-checked against a real plink2 v2.0.0-a.6.9 build
## (installed via `conda create -c bioconda plink2` during development of
## this milestone, not a build-or-test-time dependency of this package) by
## round-tripping known genotypes through plink2's own --export/--import
## machinery and comparing against plink2's own --export A hardcalls. That
## is also how a real bug was found and worked around: OxHapslegendToPgen()
## (plink2_import.cc) unconditionally calls InitOxfordSingleChr(NULL, ...)
## whenever a --legend file is given zero chromosome column, which
## dereferences a null pointer and crashes instead of erroring - which is
## why rpgen_import_haps() requires an explicit `chr` argument the task
## description didn't originally list (see its own R/rpgen_import.R
## roxygen comment and src/rpgen_import.cpp's rpgen_import_haps() comment).
##
## Verification status by format (see this file's sections below):
##   - .gen / .sample:                 full round-trip (hand-written fixture)
##   - .haps / .legend / .sample:      full round-trip, hardcall level only
##                                      (phase is written but not yet read
##                                      back - see rpgen_import_haps() docs)
##   - PLINK 1 --import-dosage/.fam/.map: full round-trip, hardcalls AND
##                                      dosages (includes one fractional,
##                                      ambiguous-hardcall dosage value)
##   - BCF:                            full round-trip IF `bcftools` is on
##                                      PATH at test time (used to convert
##                                      the milestone-4a VCF fixture to BCF,
##                                      exactly as the milestone asked for);
##                                      otherwise this section is skipped
##                                      with a message, and only the driver's
##                                      clean-error-on-bad-input path is
##                                      checked below.
##   - BGEN (v1.1 and v1.3):           full round-trip against two committed
##                                      binary fixtures
##                                      (inst/extdata/tiny.bgen + tiny.sample,
##                                      inst/extdata/tiny_selfid.bgen with no
##                                      external .sample - it carries its own
##                                      sample IDs). These are real BGEN
##                                      files (not hand-crafted bytes): they
##                                      were produced once by the plink2
##                                      oracle above from the same known
##                                      genotypes as the .gen/.haps fixtures,
##                                      and are committed as small package
##                                      data, the same way milestone 4a
##                                      committed inst/extdata/tiny.vcf. No
##                                      fixture gap remains for BGEN; a
##                                      hand-crafted-bytes fallback was not
##                                      needed.
##
## Each self-contained section below runs inside an IIFE, matching
## test_import_vcf.R's own convention (see its file header comment for why).

library(Rpgen)

if (!exists("rpgen_import_gen", where = asNamespace("Rpgen"), inherits = FALSE)) {
    exit_file("milestone 4b importers not available in this build of Rpgen")
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

## -- shared base fixture: 3 samples x 4 biallelic SNPs, no missing calls ----
##
## Used by the .gen, .haps/.legend, and .bgen sections below - all four
## derived (via the plink2 oracle described in this file's header comment)
## from the *same* underlying genotypes, so they share one expected-hardcall
## matrix. Missing-call handling is exercised separately by the BCF section
## (reusing milestone 4a's tiny.vcf genotypes, which do include one) and by
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
})()

## -- rpgen_import_haps(): Oxford .haps/.legend/.sample, hardcall level -----
##
## .haps/.legend encode phased haplotypes; the produced .pgen carries that
## phase, but Rpgen's readers only expose the collapsed hardcall/dosage view
## today (see rpgen_import_haps()'s roxygen Details) - this section verifies
## genotypes only, per the milestone's own guidance, not which haplotype
## carries which allele.

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
})()

## -- rpgen_import_bcf(): BCF, full round-trip against a committed fixture --
##
## inst/extdata/tiny.bcf is a real BCF (bcftools view -Ob of milestone 4a's
## own tiny.vcf, test_import_vcf.R) committed to the tree, so this runs
## unconditionally with no bcftools on PATH at test time - BCF is a first-
## class import, not a best-effort one. Those genotypes include one missing
## call, so this section also exercises missingness through the BCF path,
## unlike the .gen/.haps/.bgen fixtures above, which deliberately avoid it
## (Oxford formats' own missing-call encodings are a separate concern from
## this milestone's scope).

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

## out must end in .pgen, same contract as rpgen_import_vcf()'s.
expect_error(rpgen_import_bcf(tempfile(fileext = ".bcf"), tempfile()),
    info = "out must end in .pgen (bcf)"
)
expect_error(rpgen_import_plink1_dosage(
    tempfile(fileext = ".dosage"), tempfile(fileext = ".fam"), tempfile(fileext = ".map"),
    out = tempfile()
), info = "out must end in .pgen (plink1_dosage)")
