library(tinytest)
library(Rfmalloc)

# Proves the SECOND-compute-family thesis: the same fmalloc storage substrate
# that feeds pcaone's SVD (via the "bed"/"dosage" matmul tensors) can also
# feed kalis's Li & Stephens local-ancestry HMM, through a sibling accessor
# that is not a matmul codec (fmalloc_haplotypes() never registers a tensor
# codec and the object it returns never appears on either side of "%*%").
#
# kalis is a heavy, optional dependency (Suggests only): skip cleanly if it
# is not installed rather than failing the suite.
if (!requireNamespace("kalis", quietly = TRUE)) {
    exit_file("kalis not installed - skipping fmalloc/kalis integration demo")
}
library(kalis)

message("Testing that the fmalloc haplotype store is a faithful source for kalis Forward/Backward...")

(function() {
    tmp <- tempfile(fileext = ".bin")
    rt <- open_fmalloc(tmp, mode = "scratch", size_gb = 0.5)
    on.exit({
        cleanup_fmalloc(rt)
        unlink(tmp)
        # kalis's cache lives outside R's memory management; always clear it.
        try(kalis::ClearHaplotypeCache(), silent = TRUE)
    }, add = TRUE)

    set.seed(42L)
    L <- 800L   # variants
    N <- 120L   # haplotypes
    h <- matrix(sample(0:1, L * N, replace = TRUE, prob = c(0.6, 0.4)), L, N)
    storage.mode(h) <- "integer"

    # 1. Build the bit-packed fmalloc store and round-trip it exactly.
    hap <- fmalloc_haplotypes(h, runtime = rt)
    expect_false(inherits(hap, "fmalloc_tensor"))
    mat_from_store <- fmalloc_hap_materialize(hap, runtime = rt)
    expect_identical(mat_from_store[], h)

    # 2. Size a genetic map / rho / pars once (needs a cache to know L).
    kalis::CacheHaplotypes(h)
    map_cm <- cumsum(runif(L - 1L, 0.001, 0.01))
    rho <- kalis::CalcRho(diff(c(0, map_cm)))
    pars <- kalis::Parameters(rho)
    kalis::ClearHaplotypeCache()

    run_fb <- function(haps_source) {
        kalis::CacheHaplotypes(haps_source)
        fwd <- kalis::MakeForwardTable(pars)
        kalis::Forward(fwd, pars, nthreads = 2)
        bck <- kalis::MakeBackwardTable(pars)
        kalis::Backward(bck, pars, nthreads = 2)
        res <- list(
            alpha = fwd$alpha, alpha.f = fwd$alpha.f,
            beta = bck$beta, beta.f = bck$beta.f
        )
        kalis::ClearHaplotypeCache()
        res
    }

    # 3. Cache directly from the original in-memory matrix (the reference).
    direct <- run_fb(h)

    # 4. Cache from the fmalloc-store materialized matrix (no loci/hap
    #    subsetting, so kalis's own matrix path takes no R-level subset copy;
    #    it ingests straight from the fmalloc ALTREP DATAPTR).
    from_store <- run_fb(mat_from_store)

    expect_identical(direct$alpha, from_store$alpha)
    expect_identical(direct$alpha.f, from_store$alpha.f)
    expect_identical(direct$beta, from_store$beta)
    expect_identical(direct$beta.f, from_store$beta.f)

    # 5. Compression: bit-packed fmalloc store vs. the matrices a naive
    #    pipeline would hold.
    bytes_bitpacked <- length(unclass(hap))
    bytes_integer <- L * N * 4L
    bytes_double <- L * N * 8L
    expect_true(bytes_integer / bytes_bitpacked > 30)  # ~32x an integer matrix
    expect_true(bytes_double / bytes_bitpacked > 60)   # ~64x a double matrix

    message(sprintf(
        "  kalis Forward/Backward identical: fmalloc store vs. direct matrix (L=%d, N=%d)",
        L, N
    ))
    message(sprintf(
        "  compression: %d bytes bit-packed vs %d bytes integer (%.1fx) vs %d bytes double (%.1fx)",
        bytes_bitpacked, bytes_integer, bytes_integer / bytes_bitpacked,
        bytes_double, bytes_double / bytes_bitpacked
    ))
})()
