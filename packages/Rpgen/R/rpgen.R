#' Open a .pgen file and report its sample and variant counts
#'
#' Opens a PLINK 2 `.pgen` file through Rpgen's vendored pgenlib
#' (`PgfiInitPhase1()` / `PgfiInitPhase2()` / `PgrInit()`), reads its header
#' counts, and closes it again. This is a thin wrapper around the
#' `RC_rpgen_info` `.Call` entry point, itself a thin wrapper around the
#' `Rpgen_open_info` C-callable registered for other packages to link
#' against (see `inst/include/Rpgen.h`).
#'
#' @param path Path to a `.pgen` file.
#' @return A list with `n_sample` and `n_variant`, both integers.
#' @export
#' @examples
#' pgen <- system.file("extdata", "chr21_phase3_start.pgen", package = "Rpgen")
#' rpgen_info(pgen)
rpgen_info <- function(path) {
    path <- path.expand(as.character(path))
    .Call("RC_rpgen_info", path)
}

.rpgen_expand_pvar <- function(pvar) {
    if (is.null(pvar)) {
        return(NULL)
    }
    path.expand(as.character(pvar))
}

#' Read genotypes from a .pgen file as a dense R matrix
#'
#' Reads every sample and every variant from a PLINK 2 `.pgen` file through
#' Rpgen's vendored pgenlib, via `plink2::PgrGet()` (`rpgen_read_hardcalls()`)
#' or `plink2::PgrGetD()` (`rpgen_read_dosages()`). Both return a dense
#' `n_sample x n_variant` matrix, samples in rows and variants in columns,
#' matching PLINK's own `.bed` orientation. These are thin wrappers around the
#' `RC_rpgen_read_hardcalls`/`RC_rpgen_read_dosages` `.Call` entry points,
#' themselves thin wrappers around the `Rpgen_read_hardcalls`/
#' `Rpgen_read_dosages` C-callables (see `inst/include/Rpgen.h`) - the
#' lower-level counterpart to [rpgen_bed()]/[rpgen_dosage()], for callers who
#' want the plain R matrix rather than an `Rfmalloc` tensor.
#'
#' For a multiallelic variant, `plink2::PgrGet()`/`PgrGetD()` collapse every
#' ALT allele into a single non-reference count - the same encoding
#' `pgenlibr::ReadIntList()`/`ReadList()` return by calling the identical
#' pgenlib entry points. Allele identity (which ALT allele is present) is not
#' needed to produce that collapsed encoding, only for an allele-specific
#' read (`plink2::PgrGet1()`/`PgrGet1D()`, not exposed here), which is the
#' only thing pgenlibr's own `.pvar` requirement (`NewPgen(..., pvar = )`) at
#' this fixture's multiallelic-plus-dosage combination actually guards
#' against - so unlike `pgenlibr::NewPgen()`, `rpgen_read_hardcalls()`/
#' `rpgen_read_dosages()` do not require a `.pvar` for this file at all.
#'
#' @param path Path to a `.pgen` file.
#' @param pvar Path to the companion `.pvar`/`.pvar.zst` file. Accepted for
#'   API symmetry with a future allele-specific reader; not read yet (see
#'   Details) - the collapsed-ALT encoding these functions produce does not
#'   need it. Defaults to `NULL`.
#' @return `rpgen_read_hardcalls()` returns an integer matrix of `0`, `1`,
#'   `2`, or `NA` hardcall dosages. `rpgen_read_dosages()` returns a numeric
#'   matrix of dosages in `[0, 2]`, or `NA` for missing.
#' @seealso [rpgen_bed()], [rpgen_dosage()]
#' @examples
#' pgen <- system.file("extdata", "chr21_phase3_start.pgen", package = "Rpgen")
#' hc <- rpgen_read_hardcalls(pgen)
#' dim(hc)
#' ds <- rpgen_read_dosages(pgen)
#' dim(ds)
#' @export
rpgen_read_hardcalls <- function(path, pvar = NULL) {
    path <- path.expand(as.character(path))
    .Call("RC_rpgen_read_hardcalls", path, .rpgen_expand_pvar(pvar))
}

#' @rdname rpgen_read_hardcalls
#' @export
rpgen_read_dosages <- function(path, pvar = NULL) {
    path <- path.expand(as.character(path))
    .Call("RC_rpgen_read_dosages", path, .rpgen_expand_pvar(pvar))
}

#' Read a .pgen file into an Rfmalloc bed tensor
#'
#' Reads hardcalls from a PLINK 2 `.pgen` file with [rpgen_read_hardcalls()]
#' and packs them into fmalloc-backed, 2-bit `.bed` storage with
#' [Rfmalloc::fmalloc_bed()]. The genotype matrix itself is materialized once,
#' in R, as the integer matrix pgenlib decodes to; the fmalloc tensor it packs
#' into is what downstream matrix products (a genotype PCA, a GRM) stream
#' out-of-core, so the .pgen's genotypes make one trip through an ordinary R
#' matrix on the way into fmalloc storage, never a second one back out as
#' doubles.
#'
#' @param path Path to a `.pgen` file.
#' @param pvar Path to the companion `.pvar`/`.pvar.zst` file; see
#'   [rpgen_read_hardcalls()]. Defaults to `NULL`.
#' @param runtime Runtime handle from [Rfmalloc::open_fmalloc()]; defaults to
#'   the runtime established by [Rfmalloc::init_fmalloc()].
#' @return An `fmalloc_tensor` of dtype `"bed"`, `n_sample x n_variant`.
#' @seealso [rpgen_dosage()], [rpgen_read_hardcalls()]
#' @examples
#' pgen <- system.file("extdata", "chr21_phase3_start.pgen", package = "Rpgen")
#' rt <- Rfmalloc::open_fmalloc(tempfile(), size_gb = 0.5)
#' tn <- rpgen_bed(pgen, runtime = rt)
#' dim(tn)
#' Rfmalloc::cleanup_fmalloc(rt)
#' @export
rpgen_bed <- function(path, pvar = NULL, runtime = NULL) {
    g <- rpgen_read_hardcalls(path, pvar = pvar)
    Rfmalloc::fmalloc_bed(g, runtime = runtime)
}

#' Read a .pgen file into an Rfmalloc dosage tensor
#'
#' Reads dosages from a PLINK 2 `.pgen` file with [rpgen_read_dosages()] and
#' packs them into fmalloc-backed, 1-byte fixed-point storage with
#' [Rfmalloc::fmalloc_dosage()]. As with [rpgen_bed()], the genotype matrix is
#' materialized once, in R, as the numeric matrix pgenlib decodes to; the
#' fmalloc tensor it packs into is what a downstream standardized product
#' (via [Rfmalloc::fmalloc_dosage_standardize()]) streams out-of-core.
#'
#' @inheritParams rpgen_bed
#' @return An `fmalloc_tensor` of dtype `"dosage"`, `n_sample x n_variant`.
#' @seealso [rpgen_bed()], [rpgen_read_dosages()],
#'   [Rfmalloc::fmalloc_dosage_standardize()]
#' @examples
#' pgen <- system.file("extdata", "chr21_phase3_start.pgen", package = "Rpgen")
#' rt <- Rfmalloc::open_fmalloc(tempfile(), size_gb = 0.5)
#' tn <- rpgen_dosage(pgen, runtime = rt)
#' dim(tn)
#' Rfmalloc::cleanup_fmalloc(rt)
#' @export
rpgen_dosage <- function(path, pvar = NULL, runtime = NULL) {
    d <- rpgen_read_dosages(path, pvar = pvar)
    Rfmalloc::fmalloc_dosage(d, runtime = runtime)
}
