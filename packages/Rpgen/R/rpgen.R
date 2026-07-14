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

## The companion .bim/.fam path for a .bed path, by swapping the extension -
## the convention every PLINK 1 fileset (bed/bim/fam) follows. Errors if
## `bed_path` doesn't actually end in ".bed", since there is then no
## extension to swap; callers who have a differently-named companion file
## must pass it explicitly instead of relying on this default.
.rpgen_bed_sibling <- function(bed_path, ext) {
    if (!grepl("\\.bed$", bed_path)) {
        stop("cannot infer a '", ext, "' path from \"", bed_path,
            "\": it does not end in '.bed'; pass the path explicitly")
    }
    sub("\\.bed$", ext, bed_path)
}

## Bounded line count of a text file - used to size the sample/variant axes of
## a PLINK 1 .bed read from its companion .fam (one line per sample) and .bim
## (one line per variant), which carry no counts of their own the way a
## .pgen's header does. The fixed-size read avoids materializing a large
## companion or HAPS file as one character vector. suppressWarnings() covers
## readLines()'s "incomplete final line" warning: it is still a complete,
## countable record.
.rpgen_count_lines <- function(path) {
    open <- if (grepl("\\.(gz|bgz)$", tolower(path))) gzfile else file
    con <- open(path, open = "rt")
    on.exit(close(con), add = TRUE)
    count <- 0
    repeat {
        lines <- suppressWarnings(readLines(con, n = 65536L))
        count <- count + length(lines)
        if (length(lines) < 65536L) {
            break
        }
    }
    if (count <= .Machine$integer.max) as.integer(count) else count
}

#' Report a PLINK 1 .bed fileset's sample and variant counts
#'
#' A PLINK 1 `.bed` file carries no header of its own (unlike a `.pgen`'s -
#' see [rpgen_info()]): its sample and variant counts come from the companion
#' `.fam` (one line per sample) and `.bim` (one line per variant) instead.
#' This reads exactly those two line counts, with no pgenlib involvement.
#'
#' @param bed Path to a `.bed` file. The companion `.bim`/`.fam` are found by
#'   swapping the `.bed` extension.
#' @return A list with `n_sample` and `n_variant`, both integers.
#' @seealso [rpgen_info()], [rpgen_read_bed_hardcalls()]
#' @export
rpgen_bed_info <- function(bed) {
    bed <- path.expand(as.character(bed))
    bim <- .rpgen_bed_sibling(bed, ".bim")
    fam <- .rpgen_bed_sibling(bed, ".fam")
    list(
        n_sample = .rpgen_count_lines(fam),
        n_variant = .rpgen_count_lines(bim)
    )
}

#' Read genotypes from a PLINK 1 .bed fileset as a dense R matrix
#'
#' Reads every sample and every variant from a PLINK 1 `.bed`/`.bim`/`.fam`
#' fileset through Rpgen's vendored pgenlib. `PgfiInitPhase1()` opens a `.bed`
#' transparently, in the exact same code path a `.pgen` takes (its `vrtypes`
#' simply come back `NULL`) - the one real difference is that a `.bed` has no
#' header to read its sample/variant counts back from, so they are counted
#' from the companion `.fam`/`.bim` first (see [rpgen_bed_info()]) and passed
#' in explicitly. Genotypes are then read via the same `plink2::PgrGet()`
#' [rpgen_read_hardcalls()] uses; a `.bed` is biallelic hardcalls only, so
#' there is no dosage counterpart to this function. This is a thin wrapper
#' around the `RC_rpgen_read_bed_hardcalls` `.Call` entry point, itself a
#' thin wrapper around the `Rpgen_read_bed_hardcalls` C-callable (see
#' `inst/include/Rpgen.h`) - the lower-level counterpart to [rpgen_bed()] for
#' callers who want the plain R matrix rather than an `Rfmalloc` tensor.
#'
#' @param bed Path to a `.bed` file.
#' @param bim Path to the companion `.bim` file. Defaults to `bed` with its
#'   extension swapped for `.bim`.
#' @param fam Path to the companion `.fam` file. Defaults to `bed` with its
#'   extension swapped for `.fam`.
#' @return An integer matrix of `0`, `1`, `2`, or `NA` hardcall dosages,
#'   `n_sample x n_variant`, samples in rows and variants in columns.
#' @seealso [rpgen_bed()], [rpgen_bed_info()], [rpgen_read_hardcalls()]
#' @export
rpgen_read_bed_hardcalls <- function(bed, bim = NULL, fam = NULL) {
    bed <- path.expand(as.character(bed))
    bim <- if (is.null(bim)) .rpgen_bed_sibling(bed, ".bim") else path.expand(as.character(bim))
    fam <- if (is.null(fam)) .rpgen_bed_sibling(bed, ".fam") else path.expand(as.character(fam))
    .Call("RC_rpgen_read_bed_hardcalls", bed, bim, fam)
}

#' Read a .pgen or PLINK 1 .bed fileset into an Rfmalloc bed tensor
#'
#' Streams hardcalls into fmalloc-backed, 2-bit `.bed` storage. `path` selects
#' the reader: a path ending in
#' `.bed` is read with [rpgen_read_bed_hardcalls()] (PLINK 1, counts from the
#' companion `.bim`/`.fam`); anything else is read with
#' [rpgen_read_hardcalls()] (PLINK 2 `.pgen`). One pgenlib reader remains open
#' while bounded variant panels are decoded directly into an Rfmalloc-owned
#' codec sink. No full genotype matrix is allocated in R or C.
#'
#' @param path Path to a `.pgen` file, or a PLINK 1 `.bed` file.
#' @param pvar Path to the companion `.pvar`/`.pvar.zst` file; see
#'   [rpgen_read_hardcalls()]. Only used when `path` is a `.pgen`. Defaults to
#'   `NULL`.
#' @param bim,fam Paths to the companion `.bim`/`.fam` files; see
#'   [rpgen_read_bed_hardcalls()]. Only used when `path` is a `.bed`. Default
#'   to `NULL` (inferred from `path`).
#' @param runtime Runtime handle from [Rfmalloc::open_fmalloc()]; defaults to
#'   the runtime established by [Rfmalloc::init_fmalloc()].
#' @param block_size Number of variants in the transient decode panel. `NULL`
#'   chooses a panel of approximately 64 MiB.
#' @return An `fmalloc_tensor` of dtype `"bed"`, `n_sample x n_variant`.
#' @seealso [rpgen_dosage()], [rpgen_read_hardcalls()],
#'   [rpgen_read_bed_hardcalls()]
#' @examples
#' pgen <- system.file("extdata", "chr21_phase3_start.pgen", package = "Rpgen")
#' rt <- Rfmalloc::open_fmalloc(tempfile(), size_gb = 0.5)
#' tn <- rpgen_bed(pgen, runtime = rt)
#' dim(tn)
#' Rfmalloc::cleanup_fmalloc(rt)
#' @export
rpgen_bed <- function(path, pvar = NULL, bim = NULL, fam = NULL, runtime = NULL,
                      block_size = NULL) {
    path <- path.expand(as.character(path))
    is_bed <- grepl("\\.bed$", path)
    info <- if (is_bed) {
        bim <- if (is.null(bim)) .rpgen_bed_sibling(path, ".bim") else path.expand(as.character(bim))
        fam <- if (is.null(fam)) .rpgen_bed_sibling(path, ".fam") else path.expand(as.character(fam))
        list(n_sample = .rpgen_count_lines(fam), n_variant = .rpgen_count_lines(bim))
    } else {
        .rpgen_expand_pvar(pvar)
        rpgen_info(path)
    }
    .rpgen_stream_fmalloc(path, info, "bed", runtime, block_size)
}

#' Read a .pgen file into an Rfmalloc dosage tensor
#'
#' Streams dosages from one open PLINK 2 `.pgen` reader into fmalloc-backed,
#' 1-byte fixed-point storage. As with [rpgen_bed()], only one bounded variant
#' panel is decoded at a time.
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
rpgen_dosage <- function(path, pvar = NULL, runtime = NULL, block_size = NULL) {
    path <- path.expand(as.character(path))
    .rpgen_expand_pvar(pvar)
    .rpgen_stream_fmalloc(path, rpgen_info(path), "dosage", runtime, block_size)
}

#' Read fully phased haplotypes into a locus-major Rfmalloc store
#'
#' Reads hardcall phase with pgenlib's `PgrGetP()` and writes packed locus rows
#' directly into [Rfmalloc::create_fmalloc_haplotypes()] storage. The result has
#' dimensions `n_variant x (2 * n_sample)`, with the two haplotypes of each
#' sample adjacent in the second dimension. No dense haplotype matrix is
#' constructed.
#'
#' Every heterozygous call must carry phase and every genotype must be present.
#' The function errors at the first unphased heterozygote or missing genotype
#' instead of inventing phase. Multiallelic alleles are collapsed to the binary
#' reference/non-reference view returned by `PgrGetP()`.
#'
#' @inheritParams rpgen_dosage
#' @return An `Rfmalloc::fmalloc_haplotypes` object with variants in rows and
#'   haplotypes in columns.
#' @seealso [rpgen_bed()], [rpgen_dosage()],
#'   [Rfmalloc::fmalloc_hap_materialize()]
#' @export
rpgen_haplotypes <- function(path, pvar = NULL, runtime = NULL,
                             block_size = NULL) {
    path <- path.expand(as.character(path))
    .rpgen_expand_pvar(pvar)
    .rpgen_stream_fmalloc(
        path, rpgen_info(path), "haplotype", runtime, block_size
    )
}

.rpgen_stream_fmalloc <- function(path, info, kind, runtime, block_size) {
    runtime <- .rpgen_runtime(runtime)
    n_sample <- as.double(info$n_sample)
    n_variant <- as.double(info$n_variant)
    if (!is.finite(n_sample) || !is.finite(n_variant) ||
        n_sample < 1 || n_variant < 1) {
        stop("genotype source must contain at least one sample and one variant")
    }
    record_bytes <- switch(kind,
        bed = n_sample * 4,
        dosage = n_sample * 8,
        haplotype = ceiling(2 * n_sample / 8),
        f64 = n_sample * 8,
        stop("unknown genotype storage kind")
    )
    if (is.null(block_size)) {
        block_size <- max(1, floor((64 * 1024^2) / record_bytes))
    }
    if (length(block_size) != 1L || !is.finite(block_size) ||
        block_size < 1 || block_size != floor(block_size)) {
        stop("block_size must be a positive whole number")
    }
    block_size <- min(as.double(block_size), n_variant)
    payload <- .Call(
        "RC_rpgen_stream_fmalloc", path, n_sample, n_variant,
        match(kind, c("bed", "dosage", "haplotype", "f64")) - 1L,
        runtime, block_size
    )
    .rpgen_wrap_fmalloc(payload, kind, n_sample, n_variant)
}

.rpgen_runtime <- function(runtime) {
    if (is.null(runtime)) {
        runtime <- Rfmalloc::fmalloc_default_runtime()
    }
    if (!Rfmalloc::is_fmalloc_runtime(runtime)) {
        stop("runtime must be an open fmalloc runtime")
    }
    runtime
}

.rpgen_wrap_fmalloc <- function(payload, kind, n_sample, n_variant) {
    if (identical(kind, "haplotype")) {
        return(Rfmalloc::create_fmalloc_haplotypes(
            payload, c(as.integer(n_variant), as.integer(2 * n_sample))
        ))
    }
    if (identical(kind, "f64")) {
        return(Rfmalloc::as_fmalloc_matrix(
            payload, nrow = as.integer(n_sample),
            ncol = as.integer(n_variant), copy = FALSE
        ))
    }
    Rfmalloc::create_fmalloc_tensor(
        payload, dtype = kind, dim = c(as.integer(n_sample), as.integer(n_variant))
    )
}
