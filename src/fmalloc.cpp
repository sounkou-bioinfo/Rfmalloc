#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <mutex>
#include <new>
#include <random>
#include <stdexcept>
#include <string>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include "fmalloc.hpp"
#include <R.h>
#include <Rinternals.h>
#include <R_ext/Altrep.h>

enum fm_runtime_mode {
    FM_MODE_PERSISTENT = 1,
    FM_MODE_SCRATCH = 2
};

struct fm_runtime {
    struct fm_info *info;
    std::mutex mutex;
    size_t live_vectors;
    size_t external_refs;
    bool close_requested;
    fm_runtime_mode mode;
    std::string filepath;
    uint64_t file_uuid_hi;
    uint64_t file_uuid_lo;

    fm_runtime(struct fm_info *_info, fm_runtime_mode _mode, const char *_filepath,
               uint64_t _uuid_hi, uint64_t _uuid_lo)
        : info(_info), live_vectors(0), external_refs(0), close_requested(false),
          mode(_mode), filepath(_filepath), file_uuid_hi(_uuid_hi), file_uuid_lo(_uuid_lo) {}
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

static constexpr uint32_t FM_STRING_FLAG_NA = 1u;

struct fm_string_entry {
    uint64_t offset;
    uint64_t nbytes;
    int32_t encoding;
    uint32_t flags;
};

static SEXP fmalloc_runtime_tag = R_NilValue;
static SEXP fmalloc_vector_tag = R_NilValue;
static std::mutex fmalloc_allocator_mutex;
static unsigned char fmalloc_zero_length_data = 0;

static constexpr uint64_t RFM_APP_MAGIC = 0x52464d414c545231ULL; // "RFMALTR1"
static constexpr uint32_t RFM_APP_VERSION = 1;

struct rfm_app_root {
    uint64_t magic;
    uint32_t version;
    uint32_t reserved;
    uint64_t file_uuid_hi;
    uint64_t file_uuid_lo;
    uint64_t flags;
    uint64_t reserved_words[502];
};

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

static void close_unreferenced_runtime(fm_runtime *runtime)
{
    if (!runtime) {
        return;
    }

    struct fm_info *info_to_destroy = nullptr;
    fm_runtime *runtime_to_destroy = nullptr;
    {
        std::lock_guard<std::mutex> runtime_lock(runtime->mutex);
        runtime->close_requested = true;
        runtime_to_destroy = detach_runtime_if_ready(runtime, &info_to_destroy);
    }
    if (runtime_to_destroy) {
        destroy_runtime_native(runtime_to_destroy, info_to_destroy);
    }
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

static uint64_t random_u64()
{
    std::random_device rd;
    uint64_t hi = static_cast<uint64_t>(rd()) << 32;
    uint64_t lo = static_cast<uint64_t>(rd());
    return hi ^ lo;
}

static rfm_app_root *app_root(struct fm_info *info)
{
    return reinterpret_cast<rfm_app_root *>(static_cast<char *>(info->mem) + PAGE_SIZE);
}

static void ensure_app_root(struct fm_info *info)
{
    rfm_app_root *root = app_root(info);
    if (root->magic == RFM_APP_MAGIC && root->version == RFM_APP_VERSION &&
        (root->file_uuid_hi != 0 || root->file_uuid_lo != 0)) {
        return;
    }

    memset(root, 0, sizeof(*root));
    root->magic = RFM_APP_MAGIC;
    root->version = RFM_APP_VERSION;
    root->file_uuid_hi = random_u64();
    root->file_uuid_lo = random_u64();
    if (root->file_uuid_hi == 0 && root->file_uuid_lo == 0) {
        root->file_uuid_lo = 1;
    }
}

static std::string uuid_string(uint64_t hi, uint64_t lo)
{
    char buf[33];
    snprintf(buf, sizeof(buf), "%016llx%016llx",
             (unsigned long long)hi, (unsigned long long)lo);
    return std::string(buf);
}

static bool parse_uuid_string(const char *text, uint64_t *hi, uint64_t *lo)
{
    if (!text || strlen(text) != 32) {
        return false;
    }
    char hi_buf[17];
    char lo_buf[17];
    memcpy(hi_buf, text, 16);
    hi_buf[16] = '\0';
    memcpy(lo_buf, text + 16, 16);
    lo_buf[16] = '\0';
    char *end = nullptr;
    *hi = strtoull(hi_buf, &end, 16);
    if (!end || *end != '\0') {
        return false;
    }
    *lo = strtoull(lo_buf, &end, 16);
    return end && *end == '\0';
}

static fm_runtime_mode parse_runtime_mode(SEXP mode_sexp)
{
    if (TYPEOF(mode_sexp) != STRSXP || LENGTH(mode_sexp) != 1) {
        Rf_error("mode must be 'persistent' or 'scratch'");
        return FM_MODE_PERSISTENT;
    }
    const char *mode = CHAR(STRING_ELT(mode_sexp, 0));
    if (strcmp(mode, "persistent") == 0) {
        return FM_MODE_PERSISTENT;
    }
    if (strcmp(mode, "scratch") == 0) {
        return FM_MODE_SCRATCH;
    }
    Rf_error("mode must be 'persistent' or 'scratch'");
    return FM_MODE_PERSISTENT;
}

static fm_runtime *open_runtime_native(const char *filepath, size_t requested_size,
                                       fm_runtime_mode mode, bool auto_close,
                                       bool *initialized_out = nullptr)
{
    ensure_backing_file(filepath, requested_size);
    validate_backing_file(filepath);

    bool init_flag = false;
    struct fm_info *info = nullptr;

    {
        std::lock_guard<std::mutex> allocator_lock(fmalloc_allocator_mutex);
        info = fmalloc_init(filepath, &init_flag);
        if (info) {
            fmalloc_set_target(info);
            ensure_app_root(info);
        }
    }

    if (!info) {
        const char *last_error = fmalloc_get_last_error();
        if (last_error) {
            Rf_error("Failed to initialize fmalloc with file: %s: %s", filepath, last_error);
        } else {
            Rf_error("Failed to initialize fmalloc with file: %s", filepath);
        }
        return nullptr;
    }

    if (initialized_out) {
        *initialized_out = init_flag;
    }

    rfm_app_root *root = app_root(info);
    fm_runtime *runtime = new fm_runtime(info, mode, filepath, root->file_uuid_hi, root->file_uuid_lo);
    runtime->close_requested = auto_close;

    Rprintf("fmalloc initialized with file: %s (init: %s, mode: %s)\n",
            filepath, init_flag ? "true" : "false",
            mode == FM_MODE_PERSISTENT ? "persistent" : "scratch");
    return runtime;
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
    return type == VECSXP;
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
        return sizeof(fm_string_entry);
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

static void release_string_payloads_for_scratch(fm_vector *vec)
{
    if (!vec || vec->type != STRSXP || !vec->data || !vec->runtime || !vec->runtime->info) {
        return;
    }

    fm_string_entry *entries = static_cast<fm_string_entry *>(vec->data);
    char *base = static_cast<char *>(vec->runtime->info->mem);
    for (R_xlen_t i = 0; i < vec->len; i++) {
        if ((entries[i].flags & FM_STRING_FLAG_NA) == 0 && entries[i].offset != 0) {
            dlfree(static_cast<void *>(base + entries[i].offset));
        }
    }
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
            if (vec->data && runtime->mode == FM_MODE_SCRATCH) {
                if (!runtime->info) {
                    missing_info = true;
                } else {
                    std::lock_guard<std::mutex> allocator_lock(fmalloc_allocator_mutex);
                    fmalloc_set_target(runtime->info);
                    release_string_payloads_for_scratch(vec);
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

static uint64_t pointer_offset(fm_runtime *runtime, void *ptr)
{
    if (!ptr) {
        return 0;
    }
    return static_cast<uint64_t>(static_cast<char *>(ptr) - static_cast<char *>(runtime->info->mem));
}

static fm_vector *persistent_vector_from_offset(fm_runtime *runtime, SEXPTYPE type,
                                                R_xlen_t length, uint64_t offset,
                                                size_t bytes)
{
    if (!runtime || !runtime->info) {
        Rf_error("fmalloc runtime is closed");
        return nullptr;
    }
    if (!is_supported_vector_type(type)) {
        Rf_error("Unsupported vector type: %d", type);
        return nullptr;
    }
    if (bytes != payload_size_bytes(type, length)) {
        Rf_error("serialized fmalloc vector has inconsistent byte length");
        return nullptr;
    }
    if (bytes > 0) {
        if (offset >= runtime->info->len || bytes > runtime->info->len - offset) {
            Rf_error("serialized fmalloc vector points outside the backing file");
            return nullptr;
        }
    }

    void *data = bytes == 0 ? nullptr : static_cast<void *>(static_cast<char *>(runtime->info->mem) + offset);
    fm_vector *vec = new (std::nothrow) fm_vector(runtime, type, length, data, bytes);
    if (!vec) {
        Rf_error("failed to allocate fmalloc vector descriptor");
        return nullptr;
    }
    {
        std::lock_guard<std::mutex> runtime_lock(runtime->mutex);
        runtime->live_vectors++;
    }
    return vec;
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

static void sync_pointer_mirror_from_refs(fm_vector *vec)
{
    if (!is_pointer_vector_type(vec->type) || vec->len == 0 || !vec->data) {
        return;
    }

    SEXP *mirror = static_cast<SEXP *>(vec->data);
    for (R_xlen_t i = 0; i < vec->len; i++) {
        mirror[i] = VECTOR_ELT(vec->refs, i);
    }
}

static SEXP make_pointer_refs(fm_vector *vec)
{
    if (!is_pointer_vector_type(vec->type)) {
        return R_NilValue;
    }

    SEXP refs = PROTECT(Rf_allocVector(vec->type, vec->len));
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
    SET_VECTOR_ELT(vec->refs, i, value);
    if (vec->data) {
        static_cast<SEXP *>(vec->data)[i] = value;
    }
}

static fm_string_entry *string_entries(fm_vector *vec)
{
    return static_cast<fm_string_entry *>(vec->data);
}

static const char *string_bytes_from_entry(fm_vector *vec, const fm_string_entry *entry)
{
    static const char empty[] = "";
    if ((entry->flags & FM_STRING_FLAG_NA) != 0) {
        return nullptr;
    }
    if (entry->offset == 0 && entry->nbytes == 0) {
        return empty;
    }
    if (!vec->runtime || !vec->runtime->info) {
        Rf_error("fmalloc string vector runtime is closed");
        return nullptr;
    }
    if (entry->offset >= vec->runtime->info->len ||
        entry->nbytes > vec->runtime->info->len - entry->offset) {
        Rf_error("fmalloc string entry points outside the backing file");
        return nullptr;
    }
    return static_cast<const char *>(vec->runtime->info->mem) + entry->offset;
}

static SEXP string_elt_from_vec(fm_vector *vec, R_xlen_t i)
{
    fm_string_entry *entries = string_entries(vec);
    fm_string_entry *entry = entries + i;
    if ((entry->flags & FM_STRING_FLAG_NA) != 0) {
        return NA_STRING;
    }
    if (entry->nbytes > (uint64_t)std::numeric_limits<int>::max()) {
        Rf_error("fmalloc string is too large for an R CHARSXP");
        return R_NilValue;
    }
    const char *bytes = string_bytes_from_entry(vec, entry);
    return Rf_mkCharLenCE(bytes, (int)entry->nbytes, (cetype_t)entry->encoding);
}

static fm_string_entry make_string_entry(fm_vector *vec, SEXP value)
{
    fm_string_entry entry;
    memset(&entry, 0, sizeof(entry));
    entry.encoding = CE_NATIVE;

    if (value == NA_STRING) {
        entry.flags = FM_STRING_FLAG_NA;
        return entry;
    }
    if (TYPEOF(value) != CHARSXP) {
        Rf_error("ALTSTRING Set_elt value must be a CHARSXP");
        return entry;
    }

    R_xlen_t value_len = XLENGTH(value);
    if (value_len < 0 || value_len > (R_xlen_t)std::numeric_limits<int>::max()) {
        Rf_error("fmalloc string is too large for an R CHARSXP");
        return entry;
    }
    entry.nbytes = (uint64_t)value_len;
    entry.encoding = (int32_t)Rf_getCharCE(value);
    if (entry.nbytes == 0) {
        return entry;
    }

    fm_runtime *runtime = vec->runtime;
    if (!runtime) {
        Rf_error("fmalloc runtime is closed");
        return entry;
    }

    void *mem = nullptr;
    int saved_errno = 0;
    {
        std::lock_guard<std::mutex> runtime_lock(runtime->mutex);
        if (!runtime->info) {
            Rf_error("fmalloc runtime is closed");
            return entry;
        }
        std::lock_guard<std::mutex> allocator_lock(fmalloc_allocator_mutex);
        fmalloc_set_target(runtime->info);
        fmalloc_clear_last_error();
        mem = fmalloc((size_t)entry.nbytes + 1);
        saved_errno = errno;
        if (mem) {
            memcpy(mem, CHAR(value), (size_t)entry.nbytes);
            static_cast<char *>(mem)[entry.nbytes] = '\0';
            entry.offset = pointer_offset(runtime, mem);
        }
    }

    if (!mem) {
        const char *last_error = fmalloc_get_last_error();
        if (last_error) {
            Rf_error("fmalloc failed to allocate %llu string bytes: %s (errno: %d)",
                     (unsigned long long)entry.nbytes, last_error, saved_errno);
        } else {
            Rf_error("fmalloc failed to allocate %llu string bytes (errno: %d)",
                     (unsigned long long)entry.nbytes, saved_errno);
        }
    }
    return entry;
}

static void free_string_entry_for_scratch(fm_vector *vec, const fm_string_entry *entry)
{
    if (!vec || !vec->runtime || vec->runtime->mode != FM_MODE_SCRATCH ||
        !vec->runtime->info || (entry->flags & FM_STRING_FLAG_NA) != 0 || entry->offset == 0) {
        return;
    }
    std::lock_guard<std::mutex> runtime_lock(vec->runtime->mutex);
    if (!vec->runtime->info) {
        return;
    }
    std::lock_guard<std::mutex> allocator_lock(fmalloc_allocator_mutex);
    fmalloc_set_target(vec->runtime->info);
    dlfree(static_cast<char *>(vec->runtime->info->mem) + entry->offset);
}

static void set_string_element(fm_vector *vec, R_xlen_t i, SEXP value)
{
    fm_string_entry new_entry = make_string_entry(vec, value);
    fm_string_entry *entries = string_entries(vec);
    fm_string_entry old_entry = entries[i];
    entries[i] = new_entry;
    free_string_entry_for_scratch(vec, &old_entry);
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
    if (vec->type == STRSXP) {
        for (R_xlen_t i = 0; i < vec->len; i++) {
            SET_STRING_ELT(plain, i, string_elt_from_vec(vec, i));
        }
    } else if (is_pointer_vector_type(vec->type)) {
        SEXP refs = pointer_refs_from_altrep(x);
        for (R_xlen_t i = 0; i < vec->len; i++) {
            SET_VECTOR_ELT(plain, i, VECTOR_ELT(refs, i));
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
    fm_runtime *runtime = vec->runtime;
    const char *mode = runtime == nullptr ? "unknown" :
        (runtime->mode == FM_MODE_SCRATCH ? "scratch" : "persistent");
    const char *state = runtime == nullptr ? "unknown" :
        (runtime->close_requested ? "closing" : "open");
    const char *path = runtime == nullptr ? "<none>" : runtime->filepath.c_str();
    std::string uuid = runtime == nullptr ? std::string("<none>") :
        uuid_string(runtime->file_uuid_hi, runtime->file_uuid_lo);

    Rprintf("fmalloc_altrep type=%s length=%lld bytes=%zu data=%p mode=%s runtime=%s",
            type_label(vec->type), (long long)vec->len, vec->bytes, vec->data,
            mode, state);
    if (runtime != nullptr && runtime->info != nullptr && vec->data != nullptr) {
        Rprintf(" offset=%llu",
                (unsigned long long)pointer_offset(runtime, vec->data));
    } else {
        Rprintf(" offset=NA");
    }
    Rprintf(" uuid=%s file=%s\n", uuid.c_str(), path);
    return TRUE;
}

static SEXP fmalloc_altrep_duplicate(SEXP x, Rboolean deep)
{
    fm_vector *old_vec = vector_from_altrep(x);
    fm_vector *new_vec = allocate_fm_vector(old_vec->runtime, old_vec->type, old_vec->len, false);
    SEXP ans = PROTECT(fmalloc_new_altrep(new_vec));

    if (old_vec->type == STRSXP) {
        for (R_xlen_t i = 0; i < old_vec->len; i++) {
            SEXP value = PROTECT(string_elt_from_vec(old_vec, i));
            set_string_element(new_vec, i, value);
            UNPROTECT(1);
        }
    } else if (is_pointer_vector_type(old_vec->type)) {
        SEXP old_refs = PROTECT(pointer_refs_from_altrep(x));
        for (R_xlen_t i = 0; i < old_vec->len; i++) {
            SEXP value = VECTOR_ELT(old_refs, i);
            if (deep) {
                value = Rf_duplicate(value);
            }
            set_pointer_element(new_vec, i, value);
        }
        UNPROTECT(1);
    } else if (old_vec->bytes > 0) {
        memcpy(vector_data_or_dummy(new_vec), vector_data_or_dummy(old_vec), old_vec->bytes);
    }

    UNPROTECT(1);
    return ans;
}

static SEXP make_persistent_ref_state(fm_vector *vec)
{
    SEXP state = PROTECT(Rf_allocVector(VECSXP, 8));
    SEXP names = PROTECT(Rf_allocVector(STRSXP, 8));
    const char *name_values[] = {"format", "path", "uuid", "type", "length", "offset", "nbytes", "version"};
    for (int i = 0; i < 8; i++) {
        SET_STRING_ELT(names, i, Rf_mkChar(name_values[i]));
    }
    Rf_setAttrib(state, R_NamesSymbol, names);

    SET_VECTOR_ELT(state, 0, Rf_mkString("RfmallocRef"));
    SET_VECTOR_ELT(state, 1, Rf_mkString(vec->runtime->filepath.c_str()));
    std::string uuid = uuid_string(vec->runtime->file_uuid_hi, vec->runtime->file_uuid_lo);
    SET_VECTOR_ELT(state, 2, Rf_mkString(uuid.c_str()));
    SET_VECTOR_ELT(state, 3, Rf_ScalarInteger((int)vec->type));
    SET_VECTOR_ELT(state, 4, Rf_ScalarReal((double)vec->len));
    SET_VECTOR_ELT(state, 5, Rf_ScalarReal((double)pointer_offset(vec->runtime, vec->data)));
    SET_VECTOR_ELT(state, 6, Rf_ScalarReal((double)vec->bytes));
    SET_VECTOR_ELT(state, 7, Rf_ScalarInteger(1));

    UNPROTECT(2);
    return state;
}

static SEXP list_get(SEXP list, const char *name)
{
    SEXP names = Rf_getAttrib(list, R_NamesSymbol);
    if (TYPEOF(names) != STRSXP) {
        return R_NilValue;
    }
    for (R_xlen_t i = 0; i < XLENGTH(names); i++) {
        if (strcmp(CHAR(STRING_ELT(names, i)), name) == 0) {
            return VECTOR_ELT(list, i);
        }
    }
    return R_NilValue;
}

static bool is_persistent_ref_state(SEXP state)
{
    if (TYPEOF(state) != VECSXP) {
        return false;
    }
    SEXP format = list_get(state, "format");
    return TYPEOF(format) == STRSXP && LENGTH(format) == 1 &&
           strcmp(CHAR(STRING_ELT(format, 0)), "RfmallocRef") == 0;
}

static SEXP fmalloc_altrep_serialized_state(SEXP x)
{
    fm_vector *vec = vector_from_altrep(x);
    if (vec->runtime->mode == FM_MODE_PERSISTENT && !is_pointer_vector_type(vec->type)) {
        return make_persistent_ref_state(vec);
    }

    return fmalloc_plain_vector(x);
}

static SEXP fmalloc_altrep_unserialize(SEXP cls, SEXP state)
{
    (void)cls;
    if (!is_persistent_ref_state(state)) {
        return state;
    }

    SEXP path_sexp = list_get(state, "path");
    SEXP uuid_sexp = list_get(state, "uuid");
    SEXP type_sexp = list_get(state, "type");
    SEXP length_sexp = list_get(state, "length");
    SEXP offset_sexp = list_get(state, "offset");
    SEXP nbytes_sexp = list_get(state, "nbytes");

    if (TYPEOF(path_sexp) != STRSXP || LENGTH(path_sexp) != 1 ||
        TYPEOF(uuid_sexp) != STRSXP || LENGTH(uuid_sexp) != 1 ||
        TYPEOF(type_sexp) != INTSXP || LENGTH(type_sexp) != 1 ||
        TYPEOF(length_sexp) != REALSXP || LENGTH(length_sexp) != 1 ||
        TYPEOF(offset_sexp) != REALSXP || LENGTH(offset_sexp) != 1 ||
        TYPEOF(nbytes_sexp) != REALSXP || LENGTH(nbytes_sexp) != 1) {
        Rf_error("invalid Rfmalloc serialized reference");
        return R_NilValue;
    }

    uint64_t expected_hi = 0;
    uint64_t expected_lo = 0;
    if (!parse_uuid_string(CHAR(STRING_ELT(uuid_sexp, 0)), &expected_hi, &expected_lo)) {
        Rf_error("invalid Rfmalloc serialized UUID");
        return R_NilValue;
    }

    SEXPTYPE type = (SEXPTYPE)INTEGER(type_sexp)[0];
    double length_value = REAL(length_sexp)[0];
    double offset_value = REAL(offset_sexp)[0];
    double nbytes_value = REAL(nbytes_sexp)[0];
    if (!std::isfinite(length_value) || length_value < 0 ||
        length_value > (double)std::numeric_limits<R_xlen_t>::max() ||
        !std::isfinite(offset_value) || offset_value < 0 ||
        offset_value > (double)std::numeric_limits<uint64_t>::max() ||
        !std::isfinite(nbytes_value) || nbytes_value < 0 ||
        nbytes_value > (double)std::numeric_limits<size_t>::max()) {
        Rf_error("invalid Rfmalloc serialized reference dimensions");
        return R_NilValue;
    }
    R_xlen_t length = (R_xlen_t)length_value;
    uint64_t offset = (uint64_t)offset_value;
    size_t nbytes = (size_t)nbytes_value;
    if ((double)length != length_value || (double)offset != offset_value ||
        (double)nbytes != nbytes_value || !is_supported_vector_type(type) ||
        is_pointer_vector_type(type) || nbytes != payload_size_bytes(type, length)) {
        Rf_error("invalid Rfmalloc serialized reference metadata");
        return R_NilValue;
    }

    const char *path = CHAR(STRING_ELT(path_sexp, 0));
    fm_runtime *runtime = open_runtime_native(path, 32 * 1024 * 1024 + FMALLOC_OFF,
                                              FM_MODE_PERSISTENT, true);
    if (runtime->file_uuid_hi != expected_hi || runtime->file_uuid_lo != expected_lo) {
        close_unreferenced_runtime(runtime);
        Rf_error("Rfmalloc serialized reference points to a different backing file UUID");
        return R_NilValue;
    }
    if (nbytes > 0 && (offset >= runtime->info->len || nbytes > runtime->info->len - offset)) {
        close_unreferenced_runtime(runtime);
        Rf_error("serialized fmalloc vector points outside the backing file");
        return R_NilValue;
    }

    fm_vector *vec = persistent_vector_from_offset(runtime, type, length, offset, nbytes);
    return fmalloc_new_altrep(vec);
}

static SEXP fmalloc_altrep_coerce(SEXP x, int type)
{
    SEXPTYPE target_type = (SEXPTYPE)type;
    if (!is_supported_vector_type(target_type) || is_pointer_vector_type(target_type)) {
        SEXP plain = PROTECT(fmalloc_plain_vector(x));
        SEXP ans = Rf_coerceVector(plain, target_type);
        UNPROTECT(1);
        return ans;
    }

    fm_vector *old_vec = vector_from_altrep(x);
    SEXP plain = PROTECT(fmalloc_plain_vector(x));
    SEXP coerced = PROTECT(Rf_coerceVector(plain, target_type));
    fm_vector *new_vec = allocate_fm_vector(old_vec->runtime, target_type, XLENGTH(coerced), false);
    if (target_type == STRSXP) {
        for (R_xlen_t i = 0; i < XLENGTH(coerced); i++) {
            set_string_element(new_vec, i, STRING_ELT(coerced, i));
        }
    } else if (new_vec->bytes > 0) {
        memcpy(vector_data_or_dummy(new_vec), plain_vector_dataptr(coerced), new_vec->bytes);
    }
    SEXP ans = PROTECT(fmalloc_new_altrep(new_vec));
    UNPROTECT(3);
    return ans;
}

static bool subset_index_to_offset(SEXP indx, R_xlen_t i, R_xlen_t source_length,
                                   R_xlen_t *source_i)
{
    if (TYPEOF(indx) == INTSXP) {
        int value = INTEGER(indx)[i];
        if (value != NA_INTEGER && value > 0 && (R_xlen_t)value <= source_length) {
            *source_i = (R_xlen_t)value - 1;
            return true;
        }
        return false;
    }

    if (TYPEOF(indx) == REALSXP) {
        double value = REAL(indx)[i];
        if (!R_FINITE(value) || value < 1 ||
            value > (double)std::numeric_limits<R_xlen_t>::max()) {
            return false;
        }
        R_xlen_t offset = (R_xlen_t)(value - 1);
        if (offset >= 0 && offset < source_length) {
            *source_i = offset;
            return true;
        }
        return false;
    }

    return false;
}

static void set_subset_na_element(fm_vector *vec, R_xlen_t i)
{
    switch (vec->type) {
    case LGLSXP:
        static_cast<int *>(vector_data_or_dummy(vec))[i] = NA_INTEGER;
        break;
    case INTSXP:
        static_cast<int *>(vector_data_or_dummy(vec))[i] = NA_INTEGER;
        break;
    case REALSXP:
        static_cast<double *>(vector_data_or_dummy(vec))[i] = NA_REAL;
        break;
    case RAWSXP:
        static_cast<Rbyte *>(vector_data_or_dummy(vec))[i] = (Rbyte)0;
        break;
    case CPLXSXP: {
        Rcomplex value;
        value.r = NA_REAL;
        value.i = NA_REAL;
        static_cast<Rcomplex *>(vector_data_or_dummy(vec))[i] = value;
        break;
    }
    case STRSXP:
        set_string_element(vec, i, NA_STRING);
        break;
    case VECSXP:
        set_pointer_element(vec, i, R_NilValue);
        break;
    default:
        Rf_error("Unsupported vector type: %d", vec->type);
    }
}

static void copy_subset_element(fm_vector *dst, R_xlen_t dst_i, fm_vector *src, R_xlen_t src_i)
{
    switch (src->type) {
    case LGLSXP:
        static_cast<int *>(vector_data_or_dummy(dst))[dst_i] =
            static_cast<int *>(vector_data_or_dummy(src))[src_i];
        break;
    case INTSXP:
        static_cast<int *>(vector_data_or_dummy(dst))[dst_i] =
            static_cast<int *>(vector_data_or_dummy(src))[src_i];
        break;
    case REALSXP:
        static_cast<double *>(vector_data_or_dummy(dst))[dst_i] =
            static_cast<double *>(vector_data_or_dummy(src))[src_i];
        break;
    case RAWSXP:
        static_cast<Rbyte *>(vector_data_or_dummy(dst))[dst_i] =
            static_cast<Rbyte *>(vector_data_or_dummy(src))[src_i];
        break;
    case CPLXSXP:
        static_cast<Rcomplex *>(vector_data_or_dummy(dst))[dst_i] =
            static_cast<Rcomplex *>(vector_data_or_dummy(src))[src_i];
        break;
    case STRSXP: {
        SEXP value = PROTECT(string_elt_from_vec(src, src_i));
        set_string_element(dst, dst_i, value);
        UNPROTECT(1);
        break;
    }
    case VECSXP:
        set_pointer_element(dst, dst_i, VECTOR_ELT(src->refs, src_i));
        break;
    default:
        Rf_error("Unsupported vector type: %d", src->type);
    }
}

static SEXP fmalloc_altvec_extract_subset(SEXP x, SEXP indx, SEXP call)
{
    (void)call;
    if (TYPEOF(indx) != INTSXP && TYPEOF(indx) != REALSXP) {
        return nullptr;
    }

    fm_vector *src = vector_from_altrep(x);
    R_xlen_t n = XLENGTH(indx);
    fm_vector *dst = allocate_fm_vector(src->runtime, src->type, n, false);
    SEXP ans = PROTECT(fmalloc_new_altrep(dst));

    for (R_xlen_t i = 0; i < n; i++) {
        R_xlen_t src_i = 0;
        if (subset_index_to_offset(indx, i, src->len, &src_i)) {
            copy_subset_element(dst, i, src, src_i);
        } else {
            set_subset_na_element(dst, i);
        }
    }

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
    return string_elt_from_vec(vector_from_altrep(x), i);
}

static void fmalloc_altstring_set_elt(SEXP x, R_xlen_t i, SEXP value)
{
    set_string_element(vector_from_altrep(x), i, value);
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
        R_set_altvec_Extract_subset_method((cls), fmalloc_altvec_extract_subset); \
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

SEXP open_fmalloc_impl(SEXP filepath_sexp, SEXP size_gb_sexp, SEXP mode_sexp)
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
    fm_runtime_mode mode = parse_runtime_mode(mode_sexp);
    bool init_flag = false;
    fm_runtime *runtime = open_runtime_native(filepath, requested_size, mode, false, &init_flag);
    SEXP runtime_xptr = PROTECT(make_runtime_xptr(runtime));

    SEXP cls = PROTECT(Rf_mkString("fmalloc_runtime"));
    SEXP initialized_attr = PROTECT(Rf_ScalarLogical(init_flag));
    SEXP mode_attr = PROTECT(Rf_mkString(mode == FM_MODE_PERSISTENT ? "persistent" : "scratch"));
    std::string uuid = uuid_string(runtime->file_uuid_hi, runtime->file_uuid_lo);
    SEXP uuid_attr = PROTECT(Rf_mkString(uuid.c_str()));

    Rf_setAttrib(runtime_xptr, R_ClassSymbol, cls);
    Rf_setAttrib(runtime_xptr, Rf_install("initialized"), initialized_attr);
    Rf_setAttrib(runtime_xptr, Rf_install("mode"), mode_attr);
    Rf_setAttrib(runtime_xptr, Rf_install("uuid"), uuid_attr);

    UNPROTECT(5);
    return runtime_xptr;
}

// Backward-compatible C entry point. R code wraps this into logical-returning
// init_fmalloc(); new code should call open_fmalloc().
SEXP init_fmalloc_impl(SEXP filepath_sexp, SEXP size_gb_sexp, SEXP mode_sexp)
{
    SEXP runtime_xptr = PROTECT(open_fmalloc_impl(filepath_sexp, size_gb_sexp, mode_sexp));

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

static const R_CallMethodDef CallEntries[] = {
    {"open_fmalloc_impl", (DL_FUNC)&open_fmalloc_impl, 3},
    {"init_fmalloc_impl", (DL_FUNC)&init_fmalloc_impl, 3},
    {"create_fmalloc_vector_impl", (DL_FUNC)&create_fmalloc_vector_impl, 3},
    {"cleanup_fmalloc_impl", (DL_FUNC)&cleanup_fmalloc_impl, 1},
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
