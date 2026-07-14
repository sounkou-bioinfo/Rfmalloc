## Rfmalloc_tensor_decode: decode a block-aligned element range of a
## typed tensor into a caller buffer, using the tensor's own codec. This is the
## streaming read primitive an out-of-core consumer (e.g. RfmallocStatgen's
## RSVD) uses to pull one variant-column range at a time. Oracle: the same
## range sliced out of fmalloc_tensor_materialize() (whole-tensor decode).
## Exercised here through the R-level wrapper rfm_tensor_decode_range_impl().

library(tinytest)
library(Rfmalloc)

decode_range <- function(tensor, elem_offset, n_elems) {
    .Call("rfm_tensor_decode_range_impl", tensor, elem_offset, n_elems)
}

(function() {
    rt <- open_fmalloc(tempfile(fileext = ".bin"), mode = "scratch", size_gb = 0.2)
    on.exit(cleanup_fmalloc(rt), add = TRUE)

    set.seed(20260710)
    n <- 40L
    m <- 11L
    g <- matrix(sample(c(0:2, NA), n * m, replace = TRUE,
                       prob = c(.3, .35, .25, .1)), n, m)
    storage.mode(g) <- "integer"

    ## ---- bed, raw: decode variant-column ranges == materialize slices ----
    tn <- fmalloc_bed(g, runtime = rt)
    full <- matrix(fmalloc_tensor_materialize(tn)[], n, m)
    for (rng in list(c(0L, 1L), c(2L, 4L), c(7L, 4L), c(0L, m))) {
        c0 <- rng[1]; nc <- rng[2]
        got <- decode_range(tn, c0 * n, nc * n)
        want <- as.numeric(full[, (c0 + 1):(c0 + nc)])
        expect_equal(got, want, info = sprintf("bed raw cols [%d,%d)", c0, c0 + nc))
    }

    ## ---- bed, standardized: fused centre/scale rides the decode ----------
    tns <- fmalloc_bed_standardize(tn, runtime = rt)
    fulls <- matrix(fmalloc_tensor_materialize(tns)[], n, m)
    got <- decode_range(tns, 3L * n, 5L * n)
    expect_equal(got, as.numeric(fulls[, 4:8]))
    ## a decoded standardized column is centred (~0 mean) and unit-scaled
    col5 <- decode_range(tns, 4L * n, n)
    expect_true(abs(mean(col5)) < 1e-8)

    ## ---- dosage, standardized ---------------------------------------------
    d <- matrix(pmin(pmax(rnorm(n * m, 1, 0.5), 0), 2), n, m)
    tds <- fmalloc_dosage_standardize(fmalloc_dosage(d, runtime = rt), runtime = rt)
    fulld <- matrix(fmalloc_tensor_materialize(tds)[], n, m)
    expect_equal(decode_range(tds, 5L * n, 3L * n), as.numeric(fulld[, 6:8]))

    ## ---- error paths (non-zero return -> R error) -------------------------
    ## a range that straddles a variant column (bed decodes whole columns only)
    expect_error(decode_range(tn, 1L, n), "failed")
    ## out of range
    expect_error(decode_range(tn, 0L, (as.numeric(n) * m + 1)), "failed")
    ## not a tensor
    expect_error(decode_range(1:10, 0L, 2L), "failed")

    invisible(NULL)
})()
