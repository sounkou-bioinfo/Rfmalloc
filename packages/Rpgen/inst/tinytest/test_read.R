## rpgen_read_hardcalls()/rpgen_read_dosages(): milestone 2's correctness
## contract. Verify our own C reader (PgfiInitPhase1/PgfiInitPhase2/PgrInit,
## then a PgrGet()/PgrGetD() loop over every variant) agrees with pgenlibr's
## independent implementation of the same read, exercised through its R-level
## API, on the exact same file - the multiallelic-plus-dosage fixture that
## forces pgenlibr's own NewPgen() to require a .pvar (see test_open.R's
## comment), even though rpgen_read_hardcalls()/rpgen_read_dosages() do not
## need one (see their roxygen Details): plain PgrGet()/PgrGetD() collapse
## every ALT allele into one non-reference count, which needs no per-variant
## allele-identity bookkeeping, unlike the allele-specific PgrGet1()/
## PgrGet1D() pgenlibr's own safety check exists to protect.
##
## Each self-contained section below runs inside an IIFE, `(function() {
## ... })()`, exactly the pattern Rfmalloc's own tinytest files use: a bare
## top-level on.exit() in a source()d script fires as soon as its enclosing
## top-level statement finishes evaluating (source() evals one top-level
## expression at a time), not deferred to end of file, so any on.exit() whose
## effect must survive to a later statement needs a real function frame to
## register against.

library(Rpgen)

pgen_path <- system.file("extdata", "chr21_phase3_start.pgen", package = "Rpgen")
pvar_path <- system.file("extdata", "chr21_phase3_start.pvar.zst", package = "Rpgen")
expect_true(nzchar(pgen_path), info = "test fixture chr21_phase3_start.pgen is installed")

info <- rpgen_info(pgen_path)
n_sample <- info$n_sample
n_variant <- info$n_variant

## -- self-consistency (no pgenlibr required) --------------------------------

message("Testing rpgen_read_hardcalls()/rpgen_read_dosages() shape and range...")

hc <- rpgen_read_hardcalls(pgen_path)
ds <- rpgen_read_dosages(pgen_path)

expect_true(is.matrix(hc) && is.integer(hc))
expect_equal(dim(hc), c(n_sample, n_variant))
expect_true(all(hc %in% c(0L, 1L, 2L, NA_integer_)))

expect_true(is.matrix(ds) && is.double(ds))
expect_equal(dim(ds), c(n_sample, n_variant))
expect_true(all(is.na(ds) | (ds >= 0 & ds <= 2)))

## Hardcalls and dosages must agree wherever a hardcall is non-missing: a
## PgrGetD() dosage falls back to the hardcall itself when a variant has no
## explicit per-sample dosage record, so the two encodings can only differ at
## sites carrying real fractional dosage.
non_missing_hc <- !is.na(hc)
expect_true(all(abs(ds[non_missing_hc] - hc[non_missing_hc]) < 1 |
    is.na(ds[non_missing_hc])),
    info = "dosages are within 1 hardcall unit where hardcalls are non-missing")

## `pvar` is accepted but currently unused; passing it must not change the
## result or error.
expect_equal(rpgen_read_hardcalls(pgen_path, pvar = pvar_path), hc)
expect_equal(rpgen_read_dosages(pgen_path, pvar = pvar_path), ds)

## -- pgenlibr oracle cross-check ---------------------------------------------

if (requireNamespace("pgenlibr", quietly = TRUE)) {
    (function() {
        message("Cross-checking against pgenlibr::ReadIntList()/ReadList()...")

        oracle_pvar <- pgenlibr::NewPvar(pvar_path)
        on.exit(pgenlibr::ClosePvar(oracle_pvar), add = TRUE)
        oracle <- pgenlibr::NewPgen(pgen_path, pvar = oracle_pvar)
        on.exit(pgenlibr::ClosePgen(oracle), add = TRUE)

        expect_equal(pgenlibr::GetRawSampleCt(oracle), n_sample)
        expect_equal(pgenlibr::GetVariantCt(oracle), n_variant)

        oracle_hc <- pgenlibr::ReadIntList(oracle, seq_len(n_variant))
        oracle_ds <- pgenlibr::ReadList(oracle, seq_len(n_variant), meanimpute = FALSE)

        ## Both readers ultimately call the identical pgenlib entry points
        ## (plink2::PgrGet()/PgrGetD()) on the identical bytes, so the match
        ## is exact, not merely within tolerance.
        expect_equal(hc, oracle_hc,
            info = "rpgen_read_hardcalls() bit-exact vs. pgenlibr::ReadIntList()")
        expect_equal(ds, oracle_ds,
            info = "rpgen_read_dosages() bit-exact vs. pgenlibr::ReadList(meanimpute = FALSE)")
        expect_true(identical(is.na(hc), is.na(oracle_hc)))
        expect_true(identical(is.na(ds), is.na(oracle_ds)))

        ## ReadHardcalls()/Read() are pgenlibr's single-variant, allele-
        ## specific entry points (default allele_num = 2, i.e. the first ALT
        ## allele - the same allele plain PgrGet()/PgrGetD() report on for a
        ## biallelic variant). Spot-check a handful of variants directly
        ## against them too, restricted to variants with only 2 alleles
        ## (GetAlleleCt() == 2): at a true multiallelic site,
        ## ReadHardcalls()/Read() report the allele-2-specific count while
        ## rpgen_read_hardcalls()/rpgen_read_dosages() report the
        ## ALT-collapsed count, so the two are expected to differ there;
        ## that is the documented scope of this milestone's reader (see its
        ## roxygen Details), not a bug.
        biallelic_idx <- which(vapply(seq_len(min(n_variant, 50L)), function(v) {
            pgenlibr::GetAlleleCt(oracle, v) == 2L
        }, logical(1)))
        expect_true(length(biallelic_idx) > 0L,
            info = "at least one biallelic variant among the first 50 to spot-check")

        buf_num <- pgenlibr::Buf(oracle)
        buf_int <- pgenlibr::IntBuf(oracle)
        for (v in biallelic_idx) {
            pgenlibr::ReadHardcalls(oracle, buf_int, v)
            expect_equal(hc[, v], buf_int,
                info = sprintf("ReadHardcalls() matches at biallelic variant %d", v))
            pgenlibr::Read(oracle, buf_num, v)
            expect_equal(ds[, v], buf_num,
                info = sprintf("Read() matches at biallelic variant %d", v))
        }
    })()
} else {
    message("pgenlibr not installed: skipping oracle cross-check, ",
            "self-consistency assertions above still ran")
}

## -- error path ---------------------------------------------------------------

expect_error(rpgen_read_hardcalls(tempfile(fileext = ".pgen")))
expect_error(rpgen_read_dosages(tempfile(fileext = ".pgen")))

## -- rpgen_bed()/rpgen_dosage(): the fmalloc tensor surface -------------------

(function() {
    message("Testing rpgen_bed()/rpgen_dosage() against Rfmalloc::fmalloc_bed()/fmalloc_dosage()...")

    tmp <- tempfile(fileext = ".bin")
    rt <- Rfmalloc::open_fmalloc(tmp, mode = "scratch", size_gb = 0.5)
    on.exit({
        Rfmalloc::cleanup_fmalloc(rt)
        unlink(tmp)
    }, add = TRUE)

    tb <- rpgen_bed(pgen_path, runtime = rt)
    expect_true(inherits(tb, "fmalloc_tensor"))
    expect_equal(attr(tb, "rfm_dtype"), "bed")
    expect_equal(dim(tb), c(n_sample, n_variant))
    back_hc <- Rfmalloc::fmalloc_tensor_materialize(tb)[]
    expect_true(all(back_hc == hc | (is.na(back_hc) & is.na(hc))),
        info = "rpgen_bed() round-trips to the same hardcalls rpgen_read_hardcalls() reads")

    td <- rpgen_dosage(pgen_path, runtime = rt)
    expect_true(inherits(td, "fmalloc_tensor"))
    expect_equal(attr(td, "rfm_dtype"), "dosage")
    expect_equal(dim(td), c(n_sample, n_variant))
    back_ds <- Rfmalloc::fmalloc_tensor_materialize(td)[]
    ## 1-byte fixed-point dosage codec: round-trip is bounded by half its
    ## step, not exact (see Rfmalloc's fmalloc_dosage() docs).
    dos_tol <- (2 / 254) / 2 + 1e-9
    expect_true(all(is.na(back_ds) == is.na(ds)))
    expect_true(max(abs(back_ds - ds), na.rm = TRUE) <= dos_tol,
        info = "rpgen_dosage() round-trips to rpgen_read_dosages() within quantization")
})()
