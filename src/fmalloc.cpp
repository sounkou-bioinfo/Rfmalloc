#include <algorithm>
#include <cstring>
#include <limits>
#include <mutex>
#include <new>
#include <stdexcept>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include "fmalloc.hpp"
#include <R.h>
#include <Rinternals.h>
#include <R_ext/Altrep.h>

struct fm_runtime {
    struct fm_info *info;
    std::mutex mutex;
    size_t live_vectors;
    size_t external_refs;
    bool close_requested;

    explicit fm_runtime(struct fm_info *_info)
        : info(_info), live_vectors(0), external_refs(0), close_requested(false) {}
};

struct fm_vector {
    fm_runtime *runtime;
    SEXPTYPE type;
    R_xlen_t len;
    void *data;
    size_t bytes;
    SEXP refs;

    fm_vector(fm_runtime *_runtime, SEXPTYPE _type, R_xlen_t _length, void *_data, size_t _bytes)
        : runtime(_runtime), type(_type), len(_length), data(_data), bytes(_bytes), refs(R_NilValue) {}
};

static SEXP fmalloc_runtime_tag = R_NilValue;
static SEXP fmalloc_vector_tag = R_NilValue;
static std::mutex fmalloc_allocator_mutex;
static unsigned char fmalloc_zero_length_data = 0;

static R_altrep_class_t fmalloc_altlogical_class;
static R_altrep_class_t fmalloc_altinteger_class;
static R_altrep_class_t fmalloc_altreal_class;
static R_altrep_class_t fmalloc_altraw_class;
static R_altrep_class_t fmalloc_altcomplex_class;
static R_altrep_class_t fmalloc_altstring_class;
static R_altrep_class_t fmalloc_altlist_class;

// Backward-compatible default runtime used by init_fmalloc()/cleanup_fmalloc()
// wrappers. New code should pass runtime handles explicitly.
static SEXP default_runtime_xptr = R_NilValue;

static void destroy_runtime_native(fm_runtime *runtime, struct fm_info *info)
{
    if (info) {
        std::lock_guard<std::mutex> allocator_lock(fmalloc_allocator_mutex);
        fmalloc_destroy(info);
    }
    delete runtime;
}

static bool runtime_ready_to_destroy(const fm_runtime *runtime)
{
    return runtime->close_requested && runtime->live_vectors == 0 && runtime->external_refs == 0;
}

static fm_runtime *detach_runtime_if_ready(fm_runtime *runtime, struct fm_info **info_to_destroy)
{
    if (!runtime_ready_to_destroy(runtime)) {
        *info_to_destroy = nullptr;
        return nullptr;
    }

    *info_to_destroy = runtime->info;
    runtime->info = nullptr;
    return runtime;
}

static fm_runtime *runtime_from_xptr(SEXP runtime_xptr, bool allow_closed = false)
{
    if (TYPEOF(runtime_xptr) != EXTPTRSXP || R_ExternalPtrTag(runtime_xptr) != fmalloc_runtime_tag) {
        Rf_error("invalid fmalloc runtime");
        return nullptr;
    }

    fm_runtime *runtime = static_cast<fm_runtime *>(R_ExternalPtrAddr(runtime_xptr));
    if (!runtime && !allow_closed) {
        Rf_error("fmalloc runtime is closed");
        return nullptr;
    }
    return runtime;
}

static void request_runtime_close(SEXP runtime_xptr, bool clear_external_ptr)
{
    fm_runtime *runtime = runtime_from_xptr(runtime_xptr, true);
    if (!runtime) {
        return;
    }

    struct fm_info *info_to_destroy = nullptr;
    fm_runtime *runtime_to_destroy = nullptr;

    {
        std::lock_guard<std::mutex> runtime_lock(runtime->mutex);
        runtime->close_requested = true;
        if (clear_external_ptr && R_ExternalPtrAddr(runtime_xptr)) {
            if (runtime->external_refs > 0) {
                runtime->external_refs--;
            }
            R_ClearExternalPtr(runtime_xptr);
        }
        runtime_to_destroy = detach_runtime_if_ready(runtime, &info_to_destroy);
    }

    if (runtime_to_destroy) {
        destroy_runtime_native(runtime_to_destroy, info_to_destroy);
    }
}

static void fmalloc_runtime_finalizer(SEXP runtime_xptr)
{
    request_runtime_close(runtime_xptr, true);
}

static SEXP make_runtime_xptr(fm_runtime *runtime)
{
    SEXP runtime_xptr = PROTECT(R_MakeExternalPtr(runtime, fmalloc_runtime_tag, R_NilValue));
    {
        std::lock_guard<std::mutex> runtime_lock(runtime->mutex);
        runtime->external_refs++;
    }
    R_RegisterCFinalizerEx(runtime_xptr, fmalloc_runtime_finalizer, TRUE);
    UNPROTECT(1);
    return runtime_xptr;
}

static size_t parse_requested_size(SEXP size_gb_sexp)
{
    size_t requested_size = 32 * 1024 * 1024 + FMALLOC_OFF; // Default 32MB usable plus fmalloc header
    if (!Rf_isNull(size_gb_sexp)) {
        if (TYPEOF(size_gb_sexp) != REALSXP || LENGTH(size_gb_sexp) != 1) {
            Rf_error("size_gb must be a single numeric value");
            return 0;
        }
        double size_gb = REAL(size_gb_sexp)[0];
        if (size_gb <= 0) {
            Rf_error("size_gb must be positive");
            return 0;
        }
        if (size_gb > 1000) { // Safety limit: 1TB
            Rf_error("size_gb too large (maximum 1000 GB)");
            return 0;
        }
        requested_size = (size_t)(size_gb * 1024.0 * 1024.0 * 1024.0);
        Rprintf("Requested file size: %.2f GB (%zu bytes)\n", size_gb, requested_size);
    }
    return requested_size;
}

static void ensure_backing_file(const char *filepath, size_t requested_size)
{
    struct stat st;
    bool file_exists = (stat(filepath, &st) == 0);

    if (!file_exists) {
        size_t min_size = FMALLOC_MIN_CHUNK + FMALLOC_OFF;
        if (requested_size < min_size) {
            requested_size = min_size;
            Rprintf("Adjusting size to minimum required: %zu bytes\n", requested_size);
        }

        int fd = open(filepath, O_RDWR | O_CREAT, 0666);
        if (fd < 0) {
            Rf_error("Cannot create file: %s (errno: %d)", filepath, errno);
            return;
        }

        Rprintf("Creating file with size: %zu bytes (%.2f GB)\n",
                requested_size, requested_size / (1024.0 * 1024.0 * 1024.0));

        if (ftruncate(fd, requested_size) != 0) {
            close(fd);
            Rf_error("Cannot set file size for: %s (errno: %d)", filepath, errno);
            return;
        }
        close(fd);
    } else {
        Rprintf("Using existing file: %s (size: %lld bytes)\n",
                filepath, (long long)st.st_size);
    }
}

static void validate_backing_file(const char *filepath)
{
    int test_fd = open(filepath, O_RDWR);
    if (test_fd < 0) {
        Rf_error("Cannot access file: %s (errno: %d)", filepath, errno);
        return;
    }

    struct stat test_st;
    if (fstat(test_fd, &test_st) < 0) {
        close(test_fd);
        Rf_error("Cannot stat file: %s (errno: %d)", filepath, errno);
        return;
    }
    close(test_fd);

    size_t min_size = FMALLOC_MIN_CHUNK + FMALLOC_OFF;
    if ((size_t)test_st.st_size < min_size) {
        Rf_error("File too small: %s (size: %lld, minimum: %lld)",
                 filepath, (long long)test_st.st_size, (long long)min_size);
        return;
    }
}

//==============================================================================
// Direct fmalloc vector storage
//==============================================================================

static bool is_supported_vector_type(SEXPTYPE type)
{
    return type == LGLSXP || type == INTSXP || type == REALSXP ||
           type == RAWSXP || type == CPLXSXP || type == STRSXP || type == VECSXP;
}

static bool is_pointer_vector_type(SEXPTYPE type)
{
    return type == STRSXP || type == VECSXP;
}

static size_t element_size(SEXPTYPE type)
{
    switch (type) {
    case LGLSXP:
        return sizeof(int);
    case INTSXP:
        return sizeof(int);
    case REALSXP:
        return sizeof(double);
    case RAWSXP:
        return sizeof(Rbyte);
    case CPLXSXP:
        return sizeof(Rcomplex);
    case STRSXP:
        return sizeof(SEXP);
    case VECSXP:
        return sizeof(SEXP);
    default:
        Rf_error("Unsupported vector type: %d", type);
        return 0;
    }
}

static const char *type_label(SEXPTYPE type)
{
    switch (type) {
    case LGLSXP:
        return "logical";
    case INTSXP:
        return "integer";
    case REALSXP:
        return "numeric";
    case RAWSXP:
        return "raw";
    case CPLXSXP:
        return "complex";
    case STRSXP:
        return "character";
    case VECSXP:
        return "list";
    default:
        return "unsupported";
    }
}

static size_t payload_size_bytes(SEXPTYPE type, R_xlen_t length)
{
    if (length < 0) {
        Rf_error("length must be a positive integer or zero");
        return 0;
    }

    size_t elt_size = element_size(type);
    if ((uint64_t)length > (uint64_t)(std::numeric_limits<size_t>::max() / elt_size)) {
        Rf_error("requested vector is too large");
        return 0;
    }
    return (size_t)length * elt_size;
}

static fm_vector *allocate_fm_vector(fm_runtime *runtime, SEXPTYPE type, R_xlen_t length, bool require_open)
{
    if (!runtime) {
        Rf_error("fmalloc runtime is closed");
        return nullptr;
    }
    if (!is_supported_vector_type(type)) {
        Rf_error("Unsupported vector type: %d", type);
        return nullptr;
    }

    size_t bytes = payload_size_bytes(type, length);
    fm_vector *vec = new (std::nothrow) fm_vector(runtime, type, length, nullptr, bytes);
    if (!vec) {
        Rf_error("failed to allocate fmalloc vector descriptor");
        return nullptr;
    }

    void *mem = nullptr;
    bool runtime_closed = false;
    int saved_errno = 0;

    {
        std::lock_guard<std::mutex> runtime_lock(runtime->mutex);
        if (!runtime->info || (require_open && runtime->close_requested)) {
            runtime_closed = true;
        } else {
            if (bytes > 0) {
                if (bytes >= 1024 * 1024) {
                    Rprintf("Large allocation: %.2f MB requested\n", (double)bytes / (1024.0 * 1024.0));
                }
                std::lock_guard<std::mutex> allocator_lock(fmalloc_allocator_mutex);
                fmalloc_set_target(runtime->info);
                fmalloc_clear_last_error();
                mem = fmalloc(bytes);
                saved_errno = errno;
                if (mem) {
                    memset(mem, 0, bytes);
                }
            }
            if (bytes == 0 || mem) {
                runtime->live_vectors++;
            }
        }
    }

    if (runtime_closed) {
        delete vec;
        Rf_error("fmalloc runtime is closed");
        return nullptr;
    }
    if (bytes > 0 && !mem) {
        const char *last_error = fmalloc_get_last_error();
        delete vec;
        if (last_error) {
            Rf_error("fmalloc failed to allocate %zu bytes: %s (errno: %d)", bytes, last_error, saved_errno);
        } else {
            Rf_error("fmalloc failed to allocate %zu bytes (errno: %d)", bytes, saved_errno);
        }
        return nullptr;
    }

    vec->data = mem;
    if (bytes >= 1024 * 1024) {
        Rprintf("SUCCESS: fmalloc allocated %zu bytes\n", bytes);
    }
    return vec;
}

static void release_fm_vector(fm_vector *vec)
{
    if (!vec) {
        return;
    }

    fm_runtime *runtime = vec->runtime;
    bool missing_info = false;
    struct fm_info *info_to_destroy = nullptr;
    fm_runtime *runtime_to_destroy = nullptr;

    if (runtime) {
        {
            std::lock_guard<std::mutex> runtime_lock(runtime->mutex);
            if (vec->data) {
                if (!runtime->info) {
                    missing_info = true;
                } else {
                    std::lock_guard<std::mutex> allocator_lock(fmalloc_allocator_mutex);
                    fmalloc_set_target(runtime->info);
                    dlfree(vec->data);
                }
            }

            if (runtime->live_vectors > 0) {
                runtime->live_vectors--;
            }
            runtime_to_destroy = detach_runtime_if_ready(runtime, &info_to_destroy);
        }

        if (missing_info) {
            Rf_warning("fmalloc runtime was already destroyed before freeing %p", vec->data);
        }
        if (runtime_to_destroy) {
            destroy_runtime_native(runtime_to_destroy, info_to_destroy);
        }
    }

    delete vec;
}

static void fmalloc_vector_finalizer(SEXP vector_xptr)
{
    if (TYPEOF(vector_xptr) != EXTPTRSXP || R_ExternalPtrTag(vector_xptr) != fmalloc_vector_tag) {
        return;
    }
    fm_vector *vec = static_cast<fm_vector *>(R_ExternalPtrAddr(vector_xptr));
    if (!vec) {
        return;
    }
    R_ClearExternalPtr(vector_xptr);
    release_fm_vector(vec);
}

static fm_vector *vector_from_altrep(SEXP x)
{
    SEXP vector_xptr = R_altrep_data1(x);
    if (TYPEOF(vector_xptr) != EXTPTRSXP || R_ExternalPtrTag(vector_xptr) != fmalloc_vector_tag) {
        Rf_error("corrupt fmalloc ALTREP vector handle");
        return nullptr;
    }

    fm_vector *vec = static_cast<fm_vector *>(R_ExternalPtrAddr(vector_xptr));
    if (!vec) {
        Rf_error("fmalloc ALTREP vector is closed");
        return nullptr;
    }
    return vec;
}

static void *vector_data_or_dummy(fm_vector *vec)
{
    return vec->data ? vec->data : static_cast<void *>(&fmalloc_zero_length_data);
}

static R_altrep_class_t class_for_type(SEXPTYPE type)
{
    switch (type) {
    case LGLSXP:
        return fmalloc_altlogical_class;
    case INTSXP:
        return fmalloc_altinteger_class;
    case REALSXP:
        return fmalloc_altreal_class;
    case RAWSXP:
        return fmalloc_altraw_class;
    case CPLXSXP:
        return fmalloc_altcomplex_class;
    case STRSXP:
        return fmalloc_altstring_class;
    case VECSXP:
        return fmalloc_altlist_class;
    default:
        Rf_error("Unsupported vector type for fmalloc ALTREP: %d", type);
        return fmalloc_altinteger_class;
    }
}

static bool is_fmalloc_altrep(SEXP x)
{
    if (!ALTREP(x)) {
        return false;
    }
    return R_altrep_inherits(x, fmalloc_altlogical_class) ||
           R_altrep_inherits(x, fmalloc_altinteger_class) ||
           R_altrep_inherits(x, fmalloc_altreal_class) ||
           R_altrep_inherits(x, fmalloc_altraw_class) ||
           R_altrep_inherits(x, fmalloc_altcomplex_class) ||
           R_altrep_inherits(x, fmalloc_altstring_class) ||
           R_altrep_inherits(x, fmalloc_altlist_class);
}

static void sync_pointer_mirror_from_refs(fm_vector *vec)
{
    if (!is_pointer_vector_type(vec->type) || vec->len == 0 || !vec->data) {
        return;
    }

    SEXP *mirror = static_cast<SEXP *>(vec->data);
    if (vec->type == STRSXP) {
        for (R_xlen_t i = 0; i < vec->len; i++) {
            mirror[i] = STRING_ELT(vec->refs, i);
        }
    } else if (vec->type == VECSXP) {
        for (R_xlen_t i = 0; i < vec->len; i++) {
            mirror[i] = VECTOR_ELT(vec->refs, i);
        }
    }
}

static SEXP make_pointer_refs(fm_vector *vec)
{
    if (!is_pointer_vector_type(vec->type)) {
        return R_NilValue;
    }

    SEXP refs = PROTECT(Rf_allocVector(vec->type, vec->len));
    if (vec->type == STRSXP) {
        for (R_xlen_t i = 0; i < vec->len; i++) {
            SET_STRING_ELT(refs, i, R_BlankString);
        }
    }
    vec->refs = refs;
    sync_pointer_mirror_from_refs(vec);
    UNPROTECT(1);
    return refs;
}

static SEXP fmalloc_new_altrep(fm_vector *vec)
{
    SEXP refs = PROTECT(make_pointer_refs(vec));
    SEXP vector_xptr = PROTECT(R_MakeExternalPtr(vec, fmalloc_vector_tag, refs));
    R_RegisterCFinalizerEx(vector_xptr, fmalloc_vector_finalizer, TRUE);
    SEXP ans = R_new_altrep(class_for_type(vec->type), vector_xptr, R_NilValue);
    UNPROTECT(2);
    return ans;
}

static SEXP pointer_refs_from_altrep(SEXP x)
{
    SEXP vector_xptr = R_altrep_data1(x);
    return R_ExternalPtrProtected(vector_xptr);
}

static void set_pointer_element(fm_vector *vec, R_xlen_t i, SEXP value)
{
    if (vec->type == STRSXP) {
        SET_STRING_ELT(vec->refs, i, value);
        if (vec->data) {
            static_cast<SEXP *>(vec->data)[i] = value;
        }
    } else if (vec->type == VECSXP) {
        SET_VECTOR_ELT(vec->refs, i, value);
        if (vec->data) {
            static_cast<SEXP *>(vec->data)[i] = value;
        }
    }
}

static void *plain_vector_dataptr(SEXP plain)
{
    switch (TYPEOF(plain)) {
    case LGLSXP:
        return LOGICAL(plain);
    case INTSXP:
        return INTEGER(plain);
    case REALSXP:
        return REAL(plain);
    case RAWSXP:
        return RAW(plain);
    case CPLXSXP:
        return COMPLEX(plain);
    default:
        Rf_error("unsupported plain vector type: %d", TYPEOF(plain));
        return nullptr;
    }
}

static SEXP fmalloc_plain_vector(SEXP x)
{
    fm_vector *vec = vector_from_altrep(x);
    SEXP plain = PROTECT(Rf_allocVector(vec->type, vec->len));
    if (is_pointer_vector_type(vec->type)) {
        SEXP refs = pointer_refs_from_altrep(x);
        if (vec->type == STRSXP) {
            for (R_xlen_t i = 0; i < vec->len; i++) {
                SET_STRING_ELT(plain, i, STRING_ELT(refs, i));
            }
        } else {
            for (R_xlen_t i = 0; i < vec->len; i++) {
                SET_VECTOR_ELT(plain, i, VECTOR_ELT(refs, i));
            }
        }
    } else if (vec->bytes > 0) {
        memcpy(plain_vector_dataptr(plain), vector_data_or_dummy(vec), vec->bytes);
    }
    UNPROTECT(1);
    return plain;
}

//==============================================================================
// ALTREP methods
//==============================================================================

static R_xlen_t fmalloc_altrep_length(SEXP x)
{
    return vector_from_altrep(x)->len;
}

static void *fmalloc_altrep_dataptr(SEXP x, Rboolean writeable)
{
    (void)writeable;
    return vector_data_or_dummy(vector_from_altrep(x));
}

static const void *fmalloc_altrep_dataptr_or_null(SEXP x)
{
    return vector_data_or_dummy(vector_from_altrep(x));
}

static Rboolean fmalloc_altrep_inspect(SEXP x, int pre, int deep, int pvec,
                                       void (*inspect_subtree)(SEXP, int, int, int))
{
    (void)pre;
    (void)deep;
    (void)pvec;
    (void)inspect_subtree;
    fm_vector *vec = vector_from_altrep(x);
    Rprintf("fmalloc_altrep %s length=%lld data=%p bytes=%zu\n",
            type_label(vec->type), (long long)vec->len, vec->data, vec->bytes);
    return TRUE;
}

static SEXP fmalloc_altrep_duplicate(SEXP x, Rboolean deep)
{
    fm_vector *old_vec = vector_from_altrep(x);
    fm_vector *new_vec = allocate_fm_vector(old_vec->runtime, old_vec->type, old_vec->len, false);
    SEXP ans = PROTECT(fmalloc_new_altrep(new_vec));

    if (is_pointer_vector_type(old_vec->type)) {
        SEXP old_refs = PROTECT(pointer_refs_from_altrep(x));
        for (R_xlen_t i = 0; i < old_vec->len; i++) {
            SEXP value = old_vec->type == STRSXP ? STRING_ELT(old_refs, i) : VECTOR_ELT(old_refs, i);
            if (deep && old_vec->type == VECSXP) {
                value = Rf_duplicate(value);
            }
            set_pointer_element(new_vec, i, value);
        }
        UNPROTECT(1);
    } else if (old_vec->bytes > 0) {
        memcpy(vector_data_or_dummy(new_vec), vector_data_or_dummy(old_vec), old_vec->bytes);
    }

    Rf_copyMostAttrib(x, ans);
    UNPROTECT(1);
    return ans;
}

static SEXP fmalloc_altrep_serialized_state(SEXP x)
{
    SEXP plain = PROTECT(fmalloc_plain_vector(x));
    Rf_copyMostAttrib(x, plain);
    UNPROTECT(1);
    return plain;
}

static SEXP fmalloc_altrep_unserialize(SEXP cls, SEXP state)
{
    (void)cls;
    return state;
}

static SEXP fmalloc_altrep_coerce(SEXP x, int type)
{
    SEXP plain = PROTECT(fmalloc_plain_vector(x));
    SEXP ans = Rf_coerceVector(plain, (SEXPTYPE)type);
    UNPROTECT(1);
    return ans;
}

static int fmalloc_altinteger_elt(SEXP x, R_xlen_t i)
{
    fm_vector *vec = vector_from_altrep(x);
    return static_cast<int *>(vector_data_or_dummy(vec))[i];
}

static R_xlen_t fmalloc_altinteger_get_region(SEXP x, R_xlen_t i, R_xlen_t n, int *buf)
{
    fm_vector *vec = vector_from_altrep(x);
    if (i < 0 || n <= 0 || i >= vec->len) {
        return 0;
    }
    R_xlen_t count = std::min(n, vec->len - i);
    memcpy(buf, static_cast<int *>(vector_data_or_dummy(vec)) + i, (size_t)count * sizeof(int));
    return count;
}

static int fmalloc_altlogical_elt(SEXP x, R_xlen_t i)
{
    fm_vector *vec = vector_from_altrep(x);
    return static_cast<int *>(vector_data_or_dummy(vec))[i];
}

static R_xlen_t fmalloc_altlogical_get_region(SEXP x, R_xlen_t i, R_xlen_t n, int *buf)
{
    fm_vector *vec = vector_from_altrep(x);
    if (i < 0 || n <= 0 || i >= vec->len) {
        return 0;
    }
    R_xlen_t count = std::min(n, vec->len - i);
    memcpy(buf, static_cast<int *>(vector_data_or_dummy(vec)) + i, (size_t)count * sizeof(int));
    return count;
}

static double fmalloc_altreal_elt(SEXP x, R_xlen_t i)
{
    fm_vector *vec = vector_from_altrep(x);
    return static_cast<double *>(vector_data_or_dummy(vec))[i];
}

static R_xlen_t fmalloc_altreal_get_region(SEXP x, R_xlen_t i, R_xlen_t n, double *buf)
{
    fm_vector *vec = vector_from_altrep(x);
    if (i < 0 || n <= 0 || i >= vec->len) {
        return 0;
    }
    R_xlen_t count = std::min(n, vec->len - i);
    memcpy(buf, static_cast<double *>(vector_data_or_dummy(vec)) + i, (size_t)count * sizeof(double));
    return count;
}

static Rbyte fmalloc_altraw_elt(SEXP x, R_xlen_t i)
{
    fm_vector *vec = vector_from_altrep(x);
    return static_cast<Rbyte *>(vector_data_or_dummy(vec))[i];
}

static R_xlen_t fmalloc_altraw_get_region(SEXP x, R_xlen_t i, R_xlen_t n, Rbyte *buf)
{
    fm_vector *vec = vector_from_altrep(x);
    if (i < 0 || n <= 0 || i >= vec->len) {
        return 0;
    }
    R_xlen_t count = std::min(n, vec->len - i);
    memcpy(buf, static_cast<Rbyte *>(vector_data_or_dummy(vec)) + i, (size_t)count * sizeof(Rbyte));
    return count;
}

static Rcomplex fmalloc_altcomplex_elt(SEXP x, R_xlen_t i)
{
    fm_vector *vec = vector_from_altrep(x);
    return static_cast<Rcomplex *>(vector_data_or_dummy(vec))[i];
}

static R_xlen_t fmalloc_altcomplex_get_region(SEXP x, R_xlen_t i, R_xlen_t n, Rcomplex *buf)
{
    fm_vector *vec = vector_from_altrep(x);
    if (i < 0 || n <= 0 || i >= vec->len) {
        return 0;
    }
    R_xlen_t count = std::min(n, vec->len - i);
    memcpy(buf, static_cast<Rcomplex *>(vector_data_or_dummy(vec)) + i, (size_t)count * sizeof(Rcomplex));
    return count;
}

static SEXP fmalloc_altstring_elt(SEXP x, R_xlen_t i)
{
    return STRING_ELT(pointer_refs_from_altrep(x), i);
}

static void fmalloc_altstring_set_elt(SEXP x, R_xlen_t i, SEXP value)
{
    set_pointer_element(vector_from_altrep(x), i, value);
}

static SEXP fmalloc_altlist_elt(SEXP x, R_xlen_t i)
{
    return VECTOR_ELT(pointer_refs_from_altrep(x), i);
}

static void fmalloc_altlist_set_elt(SEXP x, R_xlen_t i, SEXP value)
{
    set_pointer_element(vector_from_altrep(x), i, value);
}

#define REGISTER_COMMON_ALTREP_METHODS(cls)                                      \
    do {                                                                         \
        R_set_altrep_Inspect_method((cls), fmalloc_altrep_inspect);              \
        R_set_altrep_Length_method((cls), fmalloc_altrep_length);                \
        R_set_altrep_Duplicate_method((cls), fmalloc_altrep_duplicate);          \
        R_set_altrep_Coerce_method((cls), fmalloc_altrep_coerce);                \
        R_set_altrep_Serialized_state_method((cls), fmalloc_altrep_serialized_state); \
        R_set_altrep_Unserialize_method((cls), fmalloc_altrep_unserialize);      \
    } while (0)

#define REGISTER_ATOMIC_DATAPTR_METHODS(cls)                                     \
    do {                                                                         \
        R_set_altvec_Dataptr_method((cls), fmalloc_altrep_dataptr);              \
        R_set_altvec_Dataptr_or_null_method((cls), fmalloc_altrep_dataptr_or_null); \
    } while (0)

static void register_fmalloc_altrep_classes(DllInfo *dll)
{
    fmalloc_altlogical_class = R_make_altlogical_class("fmalloc_logical", "Rfmalloc", dll);
    REGISTER_COMMON_ALTREP_METHODS(fmalloc_altlogical_class);
    REGISTER_ATOMIC_DATAPTR_METHODS(fmalloc_altlogical_class);
    R_set_altlogical_Elt_method(fmalloc_altlogical_class, fmalloc_altlogical_elt);
    R_set_altlogical_Get_region_method(fmalloc_altlogical_class, fmalloc_altlogical_get_region);

    fmalloc_altinteger_class = R_make_altinteger_class("fmalloc_integer", "Rfmalloc", dll);
    REGISTER_COMMON_ALTREP_METHODS(fmalloc_altinteger_class);
    REGISTER_ATOMIC_DATAPTR_METHODS(fmalloc_altinteger_class);
    R_set_altinteger_Elt_method(fmalloc_altinteger_class, fmalloc_altinteger_elt);
    R_set_altinteger_Get_region_method(fmalloc_altinteger_class, fmalloc_altinteger_get_region);

    fmalloc_altreal_class = R_make_altreal_class("fmalloc_real", "Rfmalloc", dll);
    REGISTER_COMMON_ALTREP_METHODS(fmalloc_altreal_class);
    REGISTER_ATOMIC_DATAPTR_METHODS(fmalloc_altreal_class);
    R_set_altreal_Elt_method(fmalloc_altreal_class, fmalloc_altreal_elt);
    R_set_altreal_Get_region_method(fmalloc_altreal_class, fmalloc_altreal_get_region);

    fmalloc_altraw_class = R_make_altraw_class("fmalloc_raw", "Rfmalloc", dll);
    REGISTER_COMMON_ALTREP_METHODS(fmalloc_altraw_class);
    REGISTER_ATOMIC_DATAPTR_METHODS(fmalloc_altraw_class);
    R_set_altraw_Elt_method(fmalloc_altraw_class, fmalloc_altraw_elt);
    R_set_altraw_Get_region_method(fmalloc_altraw_class, fmalloc_altraw_get_region);

    fmalloc_altcomplex_class = R_make_altcomplex_class("fmalloc_complex", "Rfmalloc", dll);
    REGISTER_COMMON_ALTREP_METHODS(fmalloc_altcomplex_class);
    REGISTER_ATOMIC_DATAPTR_METHODS(fmalloc_altcomplex_class);
    R_set_altcomplex_Elt_method(fmalloc_altcomplex_class, fmalloc_altcomplex_elt);
    R_set_altcomplex_Get_region_method(fmalloc_altcomplex_class, fmalloc_altcomplex_get_region);

    fmalloc_altstring_class = R_make_altstring_class("fmalloc_string", "Rfmalloc", dll);
    REGISTER_COMMON_ALTREP_METHODS(fmalloc_altstring_class);
    R_set_altstring_Elt_method(fmalloc_altstring_class, fmalloc_altstring_elt);
    R_set_altstring_Set_elt_method(fmalloc_altstring_class, fmalloc_altstring_set_elt);

    fmalloc_altlist_class = R_make_altlist_class("fmalloc_list", "Rfmalloc", dll);
    REGISTER_COMMON_ALTREP_METHODS(fmalloc_altlist_class);
    R_set_altlist_Elt_method(fmalloc_altlist_class, fmalloc_altlist_elt);
    R_set_altlist_Set_elt_method(fmalloc_altlist_class, fmalloc_altlist_set_elt);
}

extern "C" {

SEXP open_fmalloc_impl(SEXP filepath_sexp, SEXP size_gb_sexp)
{
    if (TYPEOF(filepath_sexp) != STRSXP || LENGTH(filepath_sexp) != 1) {
        Rf_error("filepath must be a single character string");
        return R_NilValue;
    }

    const char *filepath = CHAR(STRING_ELT(filepath_sexp, 0));
    if (!filepath || strlen(filepath) == 0) {
        Rf_error("filepath cannot be empty");
        return R_NilValue;
    }

    size_t requested_size = parse_requested_size(size_gb_sexp);
    ensure_backing_file(filepath, requested_size);
    validate_backing_file(filepath);

    bool init_flag = false;
    struct fm_info *info = nullptr;

    {
        std::lock_guard<std::mutex> allocator_lock(fmalloc_allocator_mutex);
        info = fmalloc_init(filepath, &init_flag);
        if (info) {
            fmalloc_set_target(info);
        }
    }

    if (!info) {
        const char *last_error = fmalloc_get_last_error();
        if (last_error) {
            Rf_error("Failed to initialize fmalloc with file: %s: %s", filepath, last_error);
        } else {
            Rf_error("Failed to initialize fmalloc with file: %s", filepath);
        }
        return R_NilValue;
    }

    fm_runtime *runtime = new fm_runtime(info);
    SEXP runtime_xptr = PROTECT(make_runtime_xptr(runtime));

    SEXP cls = PROTECT(Rf_mkString("fmalloc_runtime"));
    Rf_setAttrib(runtime_xptr, R_ClassSymbol, cls);
    Rf_setAttrib(runtime_xptr, Rf_install("initialized"), Rf_ScalarLogical(init_flag));

    Rprintf("fmalloc initialized with file: %s (init: %s)\n",
            filepath, init_flag ? "true" : "false");

    UNPROTECT(2);
    return runtime_xptr;
}

// Backward-compatible C entry point. R code wraps this into logical-returning
// init_fmalloc(); new code should call open_fmalloc().
SEXP init_fmalloc_impl(SEXP filepath_sexp, SEXP size_gb_sexp)
{
    SEXP runtime_xptr = PROTECT(open_fmalloc_impl(filepath_sexp, size_gb_sexp));

    if (default_runtime_xptr != R_NilValue) {
        R_ReleaseObject(default_runtime_xptr);
        default_runtime_xptr = R_NilValue;
    }
    default_runtime_xptr = runtime_xptr;
    R_PreserveObject(default_runtime_xptr);

    SEXP initialized = Rf_getAttrib(runtime_xptr, Rf_install("initialized"));
    int result = Rf_asLogical(initialized);
    UNPROTECT(1);
    return Rf_ScalarLogical(result);
}

SEXP create_fmalloc_vector_impl(SEXP runtime_xptr, SEXP template_vec, SEXP length_sexp)
{
    fm_runtime *runtime = runtime_from_xptr(runtime_xptr);

    if (!template_vec || TYPEOF(length_sexp) != INTSXP) {
        Rf_error("Invalid arguments to create_fmalloc_vector");
        return R_NilValue;
    }

    int length = Rf_asInteger(length_sexp);
    if (length < 0) {
        Rf_error("length must be a positive integer or zero");
        return R_NilValue;
    }

    SEXPTYPE vec_type = (SEXPTYPE)TYPEOF(template_vec);
    if (!is_supported_vector_type(vec_type)) {
        Rf_error("Unsupported vector type: %d", vec_type);
        return R_NilValue;
    }

    if (length >= 1000) {
        Rprintf("Creating fmalloc ALTREP vector: type=%s, length=%d\n", type_label(vec_type), length);
    }

    fm_vector *vec = allocate_fm_vector(runtime, vec_type, (R_xlen_t)length, true);
    SEXP result = PROTECT(fmalloc_new_altrep(vec));

    if (length >= 1000) {
        Rprintf("Successfully created fmalloc ALTREP vector\n");
    }

    UNPROTECT(1);
    return result;
}

SEXP cleanup_fmalloc_impl(SEXP runtime_xptr)
{
    if (Rf_isNull(runtime_xptr)) {
        if (default_runtime_xptr == R_NilValue) {
            Rprintf("cleanup_fmalloc called but fmalloc not initialized\n");
            return R_NilValue;
        }
        runtime_xptr = default_runtime_xptr;
        default_runtime_xptr = R_NilValue;
        R_ReleaseObject(runtime_xptr);
    }

    if (TYPEOF(runtime_xptr) != EXTPTRSXP || R_ExternalPtrTag(runtime_xptr) != fmalloc_runtime_tag) {
        Rf_error("invalid fmalloc runtime");
        return R_NilValue;
    }

    Rprintf("Cleaning up fmalloc...\n");
    request_runtime_close(runtime_xptr, true);
    Rprintf("fmalloc cleaned up\n");
    return R_NilValue;
}

SEXP is_fmalloc_altrep_impl(SEXP x)
{
    return Rf_ScalarLogical(is_fmalloc_altrep(x));
}

static const R_CallMethodDef CallEntries[] = {
    {"open_fmalloc_impl", (DL_FUNC)&open_fmalloc_impl, 2},
    {"init_fmalloc_impl", (DL_FUNC)&init_fmalloc_impl, 2},
    {"create_fmalloc_vector_impl", (DL_FUNC)&create_fmalloc_vector_impl, 3},
    {"cleanup_fmalloc_impl", (DL_FUNC)&cleanup_fmalloc_impl, 1},
    {"is_fmalloc_altrep_impl", (DL_FUNC)&is_fmalloc_altrep_impl, 1},
    {nullptr, nullptr, 0}
};

void R_init_Rfmalloc(DllInfo *dll)
{
    fmalloc_runtime_tag = Rf_install("Rfmalloc.runtime");
    fmalloc_vector_tag = Rf_install("Rfmalloc.vector");
    register_fmalloc_altrep_classes(dll);
    R_registerRoutines(dll, nullptr, CallEntries, nullptr, nullptr);
    R_useDynamicSymbols(dll, FALSE);
}

void R_unload_Rfmalloc(DllInfo *dll)
{
    (void)dll;
    if (default_runtime_xptr != R_NilValue) {
        R_ReleaseObject(default_runtime_xptr);
        default_runtime_xptr = R_NilValue;
    }
}

} // extern "C"
