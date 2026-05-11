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
    bool close_allowed = runtime->close_requested || runtime->close_pending;
    return close_allowed && runtime->live_vectors == 0 && runtime->external_refs == 0;
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

static void request_runtime_close(SEXP runtime_xptr, bool clear_external_ptr, bool mark_closed = true)
{
    fm_runtime *runtime = runtime_from_xptr(runtime_xptr, true);
    if (!runtime) {
        return;
    }

    struct fm_info *info_to_destroy = nullptr;
    fm_runtime *runtime_to_destroy = nullptr;

    {
        std::lock_guard<std::mutex> runtime_lock(runtime->mutex);
        if (mark_closed) {
            runtime->close_pending = true;
        }
        if (!runtime->close_requested && runtime->close_pending && runtime->live_vectors == 0) {
            runtime->close_requested = true;
        }
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
    request_runtime_close(runtime_xptr, true, true);
}

static void fmalloc_runtime_internal_finalizer(SEXP runtime_xptr)
{
    request_runtime_close(runtime_xptr, true, false);
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
        runtime->close_pending = true;
        if (!runtime->close_requested && runtime->close_pending && runtime->live_vectors == 0) {
            runtime->close_requested = true;
        }
        runtime_to_destroy = detach_runtime_if_ready(runtime, &info_to_destroy);
    }
    if (runtime_to_destroy) {
        destroy_runtime_native(runtime_to_destroy, info_to_destroy);
    }
}

static SEXP make_runtime_xptr(fm_runtime *runtime, bool managed = true, bool mark_closed = true)
{
    SEXP runtime_xptr = PROTECT(R_MakeExternalPtr(runtime, fmalloc_runtime_tag, R_NilValue));
    if (managed) {
        {
            std::lock_guard<std::mutex> runtime_lock(runtime->mutex);
            runtime->external_refs++;
        }
        if (mark_closed) {
            R_RegisterCFinalizerEx(runtime_xptr, fmalloc_runtime_finalizer, TRUE);
        } else {
            R_RegisterCFinalizerEx(runtime_xptr, fmalloc_runtime_internal_finalizer, TRUE);
        }
    }
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
    root->catalog_head_offset = 0;
    root->catalog_epoch = 0;
    root->catalog_count = 0;
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
