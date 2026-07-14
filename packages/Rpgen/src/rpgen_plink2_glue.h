#ifndef RPGEN_PLINK2_GLUE_H
#define RPGEN_PLINK2_GLUE_H

/* Shared jump target the CLI shim's exit()/abort() replacements
 * (rpgen_cli_shim.h) longjmp to. Every import driver in rpgen_import.cpp
 * setjmp()s it before calling into the vendored PLINK 2 closure. This file is
 * deliberately NOT compiled with
 * the shim (see src/Makevars.in/.win) - it needs the real setjmp.h, not
 * anything the shim redirects.
 *
 * Why not just have the shim's exit()/abort() call Rf_error() directly (an
 * earlier version of this shim did exactly that)? Rf_error() longjmps
 * straight to R's top-level error handler, skipping every bit of driver-
 * owned cleanup in between: the ~512 MiB bigstack arena
 * (plink2::InitBigstack()) and plink2's ChrInfo allocations would leak on
 * every single exit()/abort() path, and any FILE* pgenlib_write.cc/
 * plink2_compress_stream.cc had open would never get closed. Landing on
 * this jmp_buf first lets the driver run its own goto-cleanup exactly as
 * it would for an ordinary PglErr failure, and only then raise the R
 * condition.
 *
 * Single call path assumption: like the bigstack arena itself (see
 * tools/vendor-plink2-import/PROVENANCE.md, "Arena and defaults"), this
 * jmp_buf is process-global, not thread-local. R's evaluator serializes the
 * current callers. Any new entry point into LIBPLINK2 code that can reach
 * exit()/abort() must setjmp() this same buffer before entering the closure.
 */

#include <setjmp.h>

#ifdef __cplusplus
extern "C" {
#endif

extern jmp_buf rpgen_plink2_exit_jmp;

/* Set by rpgen_plink2_exit() just before its longjmp; read by the driver
 * once setjmp() returns nonzero. Only meaningful when rpgen_plink2_aborted
 * is 0 - rpgen_plink2_abort() does not touch it.
 */
extern int rpgen_plink2_exit_code;

/* 0 normally; set to 1 by rpgen_plink2_abort() just before its longjmp (and
 * to 0 by rpgen_plink2_exit(), so a stale 1 from an earlier call can never
 * be misread). Lets the driver tell an exit(N) from an abort() apart after
 * setjmp() returns, without relying on setjmp()'s own return value for
 * anything beyond the "!= 0" test the C standard actually guarantees is
 * portable (7.13.2.1: a setjmp() call may only appear as, or as one side
 * of a comparison against a constant forming, the entire controlling
 * expression of a selection/iteration statement - capturing its return
 * value via assignment is not one of the permitted forms).
 */
extern int rpgen_plink2_aborted;

#if defined(__GNUC__) || defined(__clang__)
#define RPGEN_NORETURN __attribute__((noreturn))
#else
#define RPGEN_NORETURN
#endif

/* What the CLI shim's `#define exit(code) rpgen_plink2_exit(code)` and
 * `#define abort() rpgen_plink2_abort()` (rpgen_cli_shim.h) resolve to.
 * Neither returns - both longjmp to rpgen_plink2_exit_jmp.
 */
void rpgen_plink2_exit(int code) RPGEN_NORETURN;
void rpgen_plink2_abort(void) RPGEN_NORETURN;

#undef RPGEN_NORETURN

#ifdef __cplusplus
}
#endif

#endif /* RPGEN_PLINK2_GLUE_H */
