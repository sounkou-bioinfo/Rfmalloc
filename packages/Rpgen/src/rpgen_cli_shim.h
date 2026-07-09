#ifndef RPGEN_CLI_SHIM_H
#define RPGEN_CLI_SHIM_H

/* Force-included (via -include, see src/Makevars.in/.win) into every
 * vendored plink2 translation unit ONLY - never into rpgen.cpp,
 * rpgen_import.cpp, rpgen_null_stream.c, or rpgen_plink2_glue.c, which
 * already talk to the R API (or real setjmp.h/stdio.h) directly. See
 * tools/vendor-plink2-import/PROVENANCE.md for why this exists: plink2 is
 * a CLI program vendored as a library, so it still calls printf()/exit()
 * and writes through the raw stdout/stderr FILE*, all of which
 * `R CMD check`'s "checking compiled code" step flags as a WARNING
 * (Writing R Extensions: compiled code must not call entry points that
 * might terminate R or write to stdout/stderr directly).
 *
 * This header does NOT touch fwrite/fputs/fprintf/fflush - those keep
 * working on real FILE* streams, which is how VcfToPgen() actually writes
 * the .pgen/.pvar/.psam trio (through pgenlib_write's and
 * plink2_compress_stream's own named FILE* handles, not stdout/stderr).
 *
 * exit()/abort() do NOT call Rf_error() directly (an earlier version of
 * this shim did). Rf_error() longjmps straight to R's top level, skipping
 * whatever driver-owned cleanup (the bigstack arena, any open FILE*) sits
 * between the vendored call site and the driver's own return - see
 * rpgen_plink2_glue.h for the setjmp/longjmp relay that unwinds to the
 * driver first instead.
 *
 * <stdio.h> and <stdlib.h> are pulled in here, before the macros below are
 * defined, so their real printf()/exit()/stdout/stderr declarations get
 * parsed once under their real names. Both headers are guarded, so any
 * later `#include <stdio.h>`/`#include <cstdlib>` inside the vendored .cc/
 * .c files is a no-op - the library declarations are never re-parsed
 * through our own macros (which would otherwise mangle them, since the
 * preprocessor does not know `exit` in a *declaration* like
 * `extern void exit(int) __attribute__((noreturn));` is not a call).
 */
#include <stdio.h>
#include <stdlib.h>

#include <R_ext/Print.h>   /* Rprintf, REprintf */

#include "rpgen_plink2_glue.h" /* rpgen_plink2_exit(), rpgen_plink2_abort() */

#ifdef __cplusplus
extern "C" {
#endif
FILE *rpgen_null_stream(void); /* shared bit-bucket, opened once */
#ifdef __cplusplus
}
#endif

#define printf  Rprintf
#define exit(code) rpgen_plink2_exit((int)(code))
#define abort()    rpgen_plink2_abort()
#undef stdout
#undef stderr
#define stdout rpgen_null_stream()
#define stderr rpgen_null_stream()

#endif /* RPGEN_CLI_SHIM_H */
