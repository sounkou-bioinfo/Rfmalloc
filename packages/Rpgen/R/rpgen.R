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
