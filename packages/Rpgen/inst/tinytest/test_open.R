## rpgen_info(): the milestone 1 correctness contract. Rpgen vendors the same
## pgenlib read subset pgenlibr does (see PROVENANCE.md); verify our own
## C-callable path (PgfiInitPhase1/PgfiInitPhase2/PgrInit -> counts) agrees
## with pgenlibr's independent implementation of the same open sequence,
## exercised through its R-level API, on the exact same file.

library(Rpgen)

pgen_path <- system.file("extdata", "chr21_phase3_start.pgen", package = "Rpgen")
expect_true(nzchar(pgen_path), info = "test fixture chr21_phase3_start.pgen is installed")

info <- rpgen_info(pgen_path)

expect_true(is.list(info))
expect_equal(names(info), c("n_sample", "n_variant"))
expect_true(is.integer(info$n_sample) && length(info$n_sample) == 1L)
expect_true(is.integer(info$n_variant) && length(info$n_variant) == 1L)
expect_true(info$n_sample > 0L)
expect_true(info$n_variant > 0L)

if (requireNamespace("pgenlibr", quietly = TRUE)) {
    ## chr21_phase3_start.pgen has multiallelic variants and dosage info
    ## present simultaneously, so pgenlibr::NewPgen() refuses to open it
    ## without a pvar object (its own consistency check - see pgenlibr.cpp's
    ## Load(): "Multiallelic variants and phase/dosage info simultaneously
    ## present; pvar required in this case"). Rpgen_open_info() has no such
    ## restriction: milestone 1 only reads the two header counts, which do
    ## not depend on per-variant allele bookkeeping. Supply the matching
    ## pvar so the oracle can open the same file at all.
    pvar_path <- system.file("extdata", "chr21_phase3_start.pvar.zst", package = "Rpgen")
    oracle_pvar <- pgenlibr::NewPvar(pvar_path)
    on.exit(pgenlibr::ClosePvar(oracle_pvar), add = TRUE)
    oracle <- pgenlibr::NewPgen(pgen_path, pvar = oracle_pvar)
    on.exit(pgenlibr::ClosePgen(oracle), add = TRUE)

    oracle_n_sample <- pgenlibr::GetRawSampleCt(oracle)
    oracle_n_variant <- pgenlibr::GetVariantCt(oracle)

    expect_equal(info$n_sample, oracle_n_sample,
        info = "Rpgen_open_info() sample count matches pgenlibr::GetRawSampleCt()")
    expect_equal(info$n_variant, oracle_n_variant,
        info = "Rpgen_open_info() variant count matches pgenlibr::GetVariantCt()")
} else {
    message("pgenlibr not installed: skipping oracle cross-check, ",
            "counts-are-positive assertions above still ran")
}

## error path: a nonexistent file must fail through R's normal condition
## system (Rf_error inside RC_rpgen_info), not crash or return garbage.
expect_error(rpgen_info(tempfile(fileext = ".pgen")))
