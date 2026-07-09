/* A shared, once-opened bit-bucket FILE* for rpgen_cli_shim.h's redirected
 * stdout/stderr macros. This translation unit is compiled WITHOUT the shim
 * (see src/Makevars.in/.win - only the vendored plink2 objects get
 * `-include rpgen_cli_shim.h`), so fopen()/stdout below are the real libc
 * ones, not the redirected macros.
 */
#include <stdio.h>

static FILE *rpgen_null_stream_handle = NULL;

FILE *rpgen_null_stream(void) {
    if (rpgen_null_stream_handle != NULL) {
        return rpgen_null_stream_handle;
    }
#ifdef _WIN32
    rpgen_null_stream_handle = fopen("NUL", "w");
#else
    rpgen_null_stream_handle = fopen("/dev/null", "w");
#endif
    if (rpgen_null_stream_handle == NULL) {
        /* Extremely unlikely fallback (e.g. a locked-down sandbox with no
         * /dev/null): any writable temp stream beats a NULL FILE*, which
         * would crash the vendored code on first write attempt.
         */
        rpgen_null_stream_handle = tmpfile();
    }
    return rpgen_null_stream_handle;
}
