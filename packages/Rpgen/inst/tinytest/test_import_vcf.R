## rpgen_import_vcf(): milestone 4a's correctness contract. Writes a tiny,
## self-contained VCF (a header with a few samples, a handful of biallelic
## SNPs with known GT fields), converts it to a .pgen through plink2's own
## VcfToPgen() importer (see src/rpgen_import.cpp), reads the produced .pgen
## back with rpgen_read_hardcalls() (milestone 2's reader, unchanged), and
## asserts the genotypes match what the VCF's GT fields encode - the whole
## point of reusing plink2's own import code being that Rpgen's existing
## reader needs no changes at all to read what it wrote.
##
## Each self-contained section below runs inside an IIFE, matching test_read.R
## / test_bed.R's own convention (see their file header comments for why: a
## bare top-level on.exit() in a source()d script fires as soon as its
## enclosing top-level statement finishes evaluating, not deferred to end of
## file).

library(Rpgen)

if (!exists("rpgen_import_vcf", where = asNamespace("Rpgen"), inherits = FALSE)) {
    exit_file("rpgen_import_vcf() not available in this build of Rpgen")
}

## -- build a tiny VCF with known genotypes -----------------------------------

## 3 samples x 4 biallelic SNPs, covering hom-ref, het, hom-alt, and one
## missing call (SAMP1 at rs3) - the same fixture inst/extdata/tiny.vcf ships
## (see rpgen_import_vcf()'s @examples), reproduced here so this test does not
## depend on the installed package layout.
vcf_lines <- c(
    "##fileformat=VCFv4.2",
    "##contig=<ID=1,length=249250621>",
    '##FORMAT=<ID=GT,Number=1,Type=String,Description="Genotype">',
    "#CHROM\tPOS\tID\tREF\tALT\tQUAL\tFILTER\tINFO\tFORMAT\tSAMP1\tSAMP2\tSAMP3",
    "1\t1000\trs1\tA\tG\t.\tPASS\t.\tGT\t0/0\t0/1\t1/1",
    "1\t2000\trs2\tC\tT\t.\tPASS\t.\tGT\t0/1\t1/1\t0/0",
    "1\t3000\trs3\tG\tA\t.\tPASS\t.\tGT\t./.\t0/0\t1/1",
    "1\t4000\trs4\tT\tC\t.\tPASS\t.\tGT\t1/1\t0/1\t0/0"
)
## Hardcall dosage encoding rpgen_read_hardcalls() uses: count of ALT alleles
## (0/1/2), NA for missing - samples in rows, variants in columns, matching
## the VCF's own SAMP1/SAMP2/SAMP3 x rs1/rs2/rs3/rs4 layout above.
expected_hc <- matrix(
    c(
        0L, 1L, 2L, # rs1: SAMP1 0/0, SAMP2 0/1, SAMP3 1/1
        1L, 2L, 0L, # rs2: SAMP1 0/1, SAMP2 1/1, SAMP3 0/0
        NA, 0L, 2L, # rs3: SAMP1 ./., SAMP2 0/0, SAMP3 1/1
        2L, 1L, 0L # rs4: SAMP1 1/1, SAMP2 0/1, SAMP3 0/0
    ),
    nrow = 3L, ncol = 4L
)

vcf_path <- tempfile(fileext = ".vcf")
writeLines(vcf_lines, vcf_path)

## -- rpgen_import_vcf(): convert, then read back with the existing reader ---
##
## Wrapped in an IIFE (see the file header comment above): its on.exit()
## cleanup must survive across every assertion in this section, which needs
## a real function frame to defer against at top level.

(function() {
    message("Testing rpgen_import_vcf() -> rpgen_read_hardcalls() round-trip...")

    pgen_path <- tempfile(fileext = ".pgen")
    import_result <- tryCatch(
        rpgen_import_vcf(vcf_path, pgen_path),
        error = function(e) e
    )
    if (inherits(import_result, "error")) {
        exit_file(paste("rpgen_import_vcf() failed in this environment:",
            conditionMessage(import_result)))
    }

    pvar_path <- sub("\\.pgen$", ".pvar", pgen_path)
    psam_path <- sub("\\.pgen$", ".psam", pgen_path)
    on.exit(unlink(c(pgen_path, pvar_path, psam_path,
        paste0(pvar_path, ".zst"), paste0(pgen_path, ".pgi"))), add = TRUE)

    expect_identical(import_result, pgen_path,
        info = "rpgen_import_vcf() returns the out path on success")
    expect_true(file.exists(pgen_path), info = "rpgen_import_vcf() wrote the .pgen")

    info <- rpgen_info(pgen_path)
    expect_equal(info$n_sample, 3L, info = "3 samples from the VCF header")
    expect_equal(info$n_variant, 4L, info = "4 variant records in the VCF")

    hc <- rpgen_read_hardcalls(pgen_path)
    expect_true(is.matrix(hc) && is.integer(hc))
    expect_equal(dim(hc), c(3L, 4L))
    expect_true(all(hc == expected_hc | (is.na(hc) & is.na(expected_hc))),
        info = "genotypes read back from the converted .pgen match the VCF's own GT fields")
})()

## -- rpgen_import_bed(): the one-call VCF -> fmalloc tensor convenience -----

(function() {
    message("Testing rpgen_import_bed() against the same known genotypes...")

    tmp <- tempfile(fileext = ".bin")
    rt <- Rfmalloc::open_fmalloc(tmp, mode = "scratch", size_gb = 0.5)
    on.exit(
        {
            Rfmalloc::cleanup_fmalloc(rt)
            unlink(tmp)
        },
        add = TRUE
    )

    tb <- rpgen_import_bed(vcf_path, runtime = rt)
    expect_true(inherits(tb, "fmalloc_tensor"))
    expect_equal(attr(tb, "rfm_dtype"), "bed")
    expect_equal(dim(tb), c(3L, 4L))

    back_hc <- Rfmalloc::fmalloc_tensor_materialize(tb)[]
    expect_true(all(back_hc == expected_hc | (is.na(back_hc) & is.na(expected_hc))),
        info = "rpgen_import_bed() round-trips the VCF's genotypes into an fmalloc tensor")
})()

## -- error paths --------------------------------------------------------------

expect_error(rpgen_import_vcf(vcf_path, tempfile()),
    info = "out must end in .pgen")
expect_error(rpgen_import_vcf(tempfile(fileext = ".vcf"), tempfile(fileext = ".pgen")),
    info = "a nonexistent VCF path is an error")

unlink(vcf_path)
