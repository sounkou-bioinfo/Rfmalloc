## rpgen_read_bed_hardcalls(): milestone 3's correctness contract, for a
## PLINK 1 .bed fileset rather than a .pgen. There is no plink2 CLI and no
## .bed fixture available in this environment, so the fixture is generated
## right here: a small known genotype matrix (0/1/2/NA) is packed into a
## byte-exact PLINK 1 .bed by hand (magic bytes, 2-bit SNP-major encoding,
## samples packed low-to-high within each byte, each variant padded to a
## byte boundary - see PgfiInitPhase1()'s "plink 1 binary" branch and
## PgrPlink1ToPlink2InplaceUnsafe() in the vendored pgenlib_read.cc for the
## exact bit mapping this encodes: 00 = hom first allele (dosage 2),
## 01 = missing, 10 = het (1), 11 = hom second allele (0)), with matching
## .bim (one line per variant) and .fam (one line per sample) companions.
## The primary oracle is then a round trip: pack the matrix, read it back
## through rpgen_read_bed_hardcalls(), and require identical() to the
## original. pgenlibr::NewPgen()/ReadIntList() is used as a secondary,
## independent oracle when available.

library(Rpgen)

## -- build a small known genotype matrix, and a byte-exact .bed for it -----

## 7 samples (not a multiple of 4, to exercise the last-byte padding) x
## 5 variants, covering every hardcall value and a couple of edge cases
## (an all-missing variant, an all-hom-first-allele variant, an
## all-hom-second-allele variant).
hc <- matrix(c(
    2L,  1L,  0L, NA,  2L,  1L,  0L,   # variant 1: mixed
    0L,  0L,  0L,  0L,  0L,  0L,  0L,  # variant 2: all hom second allele
    NA,  NA,  NA,  NA,  NA,  NA,  NA,  # variant 3: all missing
    2L,  2L,  2L,  2L,  2L,  2L,  2L,  # variant 4: all hom first allele
    1L,  0L,  2L, NA,  1L,  2L,  0L    # variant 5: mixed
), nrow = 7L, ncol = 5L)
storage.mode(hc) <- "integer"
n_sample <- nrow(hc)
n_variant <- ncol(hc)

## PLINK 1's per-sample 2-bit code (see the file header comment above):
## 00 = hom first allele (hardcall dosage 2), 01 = missing,
## 10 = het (1), 11 = hom second allele (0).
.val_to_plink1_code <- function(v) {
    if (is.na(v)) {
        return(1L)
    }
    switch(v + 1L, 3L, 2L, 0L) # v == 0 -> 3, v == 1 -> 2, v == 2 -> 0
}

## Packs one variant's genotype column into its PLINK 1 record: samples
## packed low-to-high within each byte (sample 1 in bits 0-1 of the first
## byte, sample 2 in bits 2-3, ...), the record padded to a whole byte.
.pack_bed_variant <- function(col) {
    n <- length(col)
    nbytes <- as.integer(ceiling(n / 4))
    bytes <- integer(nbytes)
    for (i in seq_len(n)) {
        code <- .val_to_plink1_code(col[i])
        byte_idx <- ((i - 1L) %/% 4L) + 1L
        bit_pos <- ((i - 1L) %% 4L) * 2L
        bytes[byte_idx] <- bitwOr(bytes[byte_idx], bitwShiftL(code, bit_pos))
    }
    as.raw(bytes)
}

## Writes `geno` (samples x variants) as a SNP-major PLINK 1 .bed: the 3
## magic/mode bytes (0x6c, 0x1b, 0x01), then one padded record per variant.
.write_bed <- function(path, geno) {
    magic <- as.raw(c(0x6c, 0x1b, 0x01))
    body <- do.call(c, lapply(seq_len(ncol(geno)), function(j) {
        .pack_bed_variant(geno[, j])
    }))
    con <- file(path, "wb")
    on.exit(close(con), add = TRUE)
    writeBin(c(magic, body), con)
}

tmpdir <- tempfile("rpgen_bed_")
dir.create(tmpdir)
bed_path <- file.path(tmpdir, "toy.bed")
bim_path <- file.path(tmpdir, "toy.bim")
fam_path <- file.path(tmpdir, "toy.fam")

.write_bed(bed_path, hc)
writeLines(sprintf("1\tvar%d\t0\t%d\tA\tG", seq_len(n_variant), seq_len(n_variant) * 1000L), bim_path)
writeLines(sprintf("fam1\tid%d\t0\t0\t1\t-9", seq_len(n_sample)), fam_path)

## -- rpgen_bed_info(): counts from .bim/.fam, no pgenlib involved ----------

message("Testing rpgen_bed_info()...")

info <- rpgen_bed_info(bed_path)
expect_true(is.list(info))
expect_equal(names(info), c("n_sample", "n_variant"))
expect_identical(info$n_sample, n_sample)
expect_identical(info$n_variant, n_variant)

## -- rpgen_read_bed_hardcalls(): the round-trip oracle ----------------------

message("Testing rpgen_read_bed_hardcalls() round-trip against the known genotype matrix...")

## Default .bim/.fam: same basename as bed_path, extension swapped.
res <- rpgen_read_bed_hardcalls(bed_path)
expect_true(is.matrix(res) && is.integer(res))
expect_equal(dim(res), c(n_sample, n_variant))
expect_identical(res, hc,
    info = "rpgen_read_bed_hardcalls() round-trips exactly to the original genotype matrix")

## Explicit .bim/.fam paths must give the identical result.
res_explicit <- rpgen_read_bed_hardcalls(bed_path, bim = bim_path, fam = fam_path)
expect_identical(res_explicit, hc)

## -- pgenlibr oracle cross-check (secondary, independent implementation) ---

if (requireNamespace("pgenlibr", quietly = TRUE)) {
    (function() {
        message("Cross-checking against pgenlibr::NewPgen()/ReadIntList() on the same .bed...")

        oracle <- pgenlibr::NewPgen(bed_path, raw_sample_ct = n_sample)
        on.exit(pgenlibr::ClosePgen(oracle), add = TRUE)

        expect_equal(pgenlibr::GetRawSampleCt(oracle), n_sample)
        expect_equal(pgenlibr::GetVariantCt(oracle), n_variant)

        oracle_hc <- pgenlibr::ReadIntList(oracle, seq_len(n_variant))
        expect_equal(res, oracle_hc,
            info = "rpgen_read_bed_hardcalls() bit-exact vs. pgenlibr::ReadIntList() on the same .bed")
    })()
} else {
    message("pgenlibr not installed: skipping oracle cross-check, ",
        "round-trip assertions above still ran")
}

## -- error paths -------------------------------------------------------------

## A .fam whose line count doesn't match the .bed's byte-packed sample count
## makes pgenlib's file-size consistency check fail (there is no other
## validation available for a header-less format).
(function() {
    bad_fam <- file.path(tmpdir, "bad.fam")
    writeLines(sprintf("fam1\tid%d\t0\t0\t1\t-9", seq_len(3L)), bad_fam)
    expect_error(rpgen_read_bed_hardcalls(bed_path, fam = bad_fam))
})()

expect_error(rpgen_bed_info(tempfile(fileext = ".notbed")),
    info = "rpgen_bed_info() requires a path ending in .bed to infer .bim/.fam")
expect_error(rpgen_read_bed_hardcalls(tempfile(fileext = ".bed")))

## -- rpgen_bed(): dispatches to the .bed reader and packs an fmalloc tensor -

(function() {
    message("Testing rpgen_bed() on a .bed path against Rfmalloc::fmalloc_bed()...")

    tmp <- tempfile(fileext = ".bin")
    rt <- Rfmalloc::open_fmalloc(tmp, mode = "scratch", size_gb = 0.5)
    on.exit({
        Rfmalloc::cleanup_fmalloc(rt)
        unlink(tmp)
    }, add = TRUE)

    tb <- rpgen_bed(bed_path, runtime = rt)
    expect_true(inherits(tb, "fmalloc_tensor"))
    expect_equal(attr(tb, "rfm_dtype"), "bed")
    expect_equal(dim(tb), c(n_sample, n_variant))

    back_hc <- Rfmalloc::fmalloc_tensor_materialize(tb)[]
    expect_true(all(back_hc == hc | (is.na(back_hc) & is.na(hc))),
        info = "rpgen_bed() on a .bed path round-trips to the original genotypes")
})()

unlink(tmpdir, recursive = TRUE)
