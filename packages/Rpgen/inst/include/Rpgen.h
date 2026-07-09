#ifndef RPGEN_API_PUBLIC_H
#define RPGEN_API_PUBLIC_H

/*
 * Rpgen.h - C-callable API for the Rpgen carrier package, version 1.
 *
 * Rpgen vendors the read subset of PLINK 2's pgenlib
 * (https://github.com/chrchang/plink-ng) - the same subset the CRAN package
 * 'pgenlibr' vendors and builds as libPLINK2.a - and exposes it through
 * R_RegisterCCallable() entry points. Unlike pgenlibr, which only offers an
 * R-level (Rcpp) interface, Rpgen's whole reason to exist is letting other R
 * packages link to a compiled pgenlib and read .pgen genotypes natively from
 * their own C or C++ code, without re-vendoring pgenlib themselves.
 *
 * Usage:
 *   1. List Rpgen under LinkingTo (and Imports, so it is loaded first) in
 *      your package's DESCRIPTION.
 *   2. #include <Rpgen.h> in your C/C++ source.
 *   3. Call R_GetCCallable("Rpgen", "Rpgen_open_info") (or use the
 *      Rpgen_open_info_fun typedef below to keep the call site typed) to
 *      resolve the symbol, then invoke it. R_GetCCallable() aborts the R
 *      session if Rpgen has not been loaded yet, so only call it once
 *      Rpgen is guaranteed attached/loaded (normal LinkingTo + Imports
 *      behavior guarantees this).
 *
 * Milestone 1 provides one operation: open a .pgen file far enough to read
 * its header counts, then close it again. It does not yet keep a reader
 * handle alive across calls - that is for a later milestone, once genotype
 * reading is added.
 */

#include <stddef.h>
#include <stdint.h>

#include <R_ext/Rdynload.h>

#ifdef __cplusplus
extern "C" {
#endif

/* -- version / identity ---------------------------------------------------- */

typedef int (*Rpgen_api_version_fun)(void);

/* -- open a .pgen far enough to read its header counts ---------------------- */

/*
 * Rpgen_open_info(path, n_sample_out, n_variant_out, errbuf, errbuf_len)
 *
 * Opens the .pgen file at `path` (PgfiInitPhase1 / PgfiInitPhase2 / PgrInit),
 * reads plink2::PgenFileInfo's raw_sample_ct / raw_variant_ct, and closes
 * everything again. Returns 0 on success, with *n_sample_out/*n_variant_out
 * filled in; returns nonzero on failure, with a NUL-terminated message
 * written into errbuf (a caller-owned buffer of at least errbuf_len bytes).
 */
typedef int (*Rpgen_open_info_fun)(const char *path, uint32_t *n_sample_out,
                                   uint32_t *n_variant_out, char *errbuf,
                                   size_t errbuf_len);

#ifdef __cplusplus
}
#endif

#endif /* RPGEN_API_PUBLIC_H */
