/* Official GGUF metadata context plus a read-only mapping of tensor bytes. */
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
# include "rgguf_win_mman.h"
# include <io.h>
# define close _close
# ifndef O_BINARY
#  define O_BINARY _O_BINARY
# endif
#else
# include <sys/mman.h>
# include <unistd.h>
# define O_BINARY 0
#endif

#include "rgguf.h"

SEXP Rgguf_ctx_tag = NULL;

static void rgguf_close_native(rgguf_ctx *ctx)
{
    if (!ctx) return;
    if (ctx->mapping && ctx->mapping != MAP_FAILED) munmap(ctx->mapping, ctx->size);
    if (ctx->fd >= 0) close(ctx->fd);
    if (ctx->meta) Rggml_gguf_close_ptr()(ctx->meta);
    free(ctx);
}

static void rgguf_finalize_ctx(SEXP x)
{
    rgguf_ctx *ctx = (rgguf_ctx *)R_ExternalPtrAddr(x);
    if (ctx) {
        rgguf_close_native(ctx);
        R_ClearExternalPtr(x);
    }
}

rgguf_ctx *rgguf_get_ctx(SEXP x)
{
    if (TYPEOF(x) != EXTPTRSXP || R_ExternalPtrTag(x) != Rgguf_ctx_tag)
        Rf_error("not a valid 'gguf_ctx' object");
    rgguf_ctx *ctx = (rgguf_ctx *)R_ExternalPtrAddr(x);
    if (!ctx) Rf_error("gguf context has already been closed");
    return ctx;
}

SEXP RC_gguf_open(SEXP path)
{
    if (TYPEOF(path) != STRSXP || XLENGTH(path) != 1 ||
        STRING_ELT(path, 0) == NA_STRING)
        Rf_error("path must be a single non-missing string");
    const char *filename = CHAR(STRING_ELT(path, 0));

    rgguf_ctx *ctx = (rgguf_ctx *)calloc(1, sizeof(*ctx));
    if (!ctx) Rf_error("out of memory opening GGUF file");
    ctx->fd = -1;
    ctx->meta = Rggml_gguf_open_ptr()(filename);
    if (!ctx->meta) {
        free(ctx);
        Rf_error("failed to open or parse GGUF file '%s' with Rggml", filename);
    }
    ctx->data_offset = Rggml_gguf_data_offset_ptr()(ctx->meta);

    ctx->fd = open(filename, O_RDONLY | O_BINARY);
    if (ctx->fd < 0) {
        int saved = errno;
        rgguf_close_native(ctx);
        Rf_error("failed to open GGUF tensor data '%s': %s", filename,
                 strerror(saved));
    }
    struct stat st;
    if (fstat(ctx->fd, &st) != 0) {
        int saved = errno;
        rgguf_close_native(ctx);
        Rf_error("failed to stat GGUF tensor data '%s': %s", filename,
                 strerror(saved));
    }
    if (st.st_size <= 0 || (uintmax_t)st.st_size > (uintmax_t)SIZE_MAX) {
        rgguf_close_native(ctx);
        Rf_error("GGUF file '%s' has an unsupported size", filename);
    }
    ctx->size = (size_t)st.st_size;
    ctx->mapping = mmap(NULL, ctx->size, PROT_READ, MAP_PRIVATE, ctx->fd, 0);
    if (ctx->mapping == MAP_FAILED) {
        int saved = errno;
        ctx->mapping = NULL;
        rgguf_close_native(ctx);
        Rf_error("failed to map GGUF file '%s': %s", filename, strerror(saved));
    }

    SEXP ans = PROTECT(R_MakeExternalPtr(ctx, Rgguf_ctx_tag, R_NilValue));
    R_RegisterCFinalizerEx(ans, rgguf_finalize_ctx, TRUE);
    UNPROTECT(1);
    return ans;
}

SEXP RC_gguf_close(SEXP x)
{
    if (TYPEOF(x) != EXTPTRSXP || R_ExternalPtrTag(x) != Rgguf_ctx_tag)
        Rf_error("not a valid 'gguf_ctx' object");
    rgguf_ctx *ctx = (rgguf_ctx *)R_ExternalPtrAddr(x);
    if (ctx) {
        rgguf_close_native(ctx);
        R_ClearExternalPtr(x);
    }
    return R_NilValue;
}
