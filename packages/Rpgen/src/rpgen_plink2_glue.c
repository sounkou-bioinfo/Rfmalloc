/* Storage + the exit()/abort() replacements themselves - see
 * rpgen_plink2_glue.h for why this indirection exists. Compiled WITHOUT
 * the CLI shim (see src/Makevars.in/.win): it needs the real setjmp.h/
 * longjmp, not anything rpgen_cli_shim.h redirects.
 */
#include "rpgen_plink2_glue.h"

jmp_buf rpgen_plink2_exit_jmp;
int rpgen_plink2_exit_code;
int rpgen_plink2_aborted;

void rpgen_plink2_exit(int code) {
    rpgen_plink2_exit_code = code;
    rpgen_plink2_aborted = 0;
    longjmp(rpgen_plink2_exit_jmp, 1);
}

void rpgen_plink2_abort(void) {
    rpgen_plink2_aborted = 1;
    longjmp(rpgen_plink2_exit_jmp, 1);
}
