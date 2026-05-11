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

static uint64_t pointer_offset(fm_runtime *runtime, void *ptr);
static fm_vector *maybe_vector_from_altrep(SEXP x);
static rfm_catalog_record *catalog_record_from_offset(fm_runtime *runtime, uint64_t offset);
static rfm_catalog_record *create_catalog_record_locked(fm_runtime *runtime, SEXPTYPE type,
                                                        R_xlen_t length, void *payload,
                                                        size_t bytes);

static fm_vector *allocate_fm_vector(fm_runtime *runtime, SEXPTYPE type, R_xlen_t length, bool require_open, bool zero_initialize)
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
    bool catalog_failed = false;
    int saved_errno = 0;

    {
        std::lock_guard<std::mutex> runtime_lock(runtime->mutex);
        if (!runtime->info || (require_open && (runtime->close_requested || runtime->close_pending))) {
            runtime_closed = true;
        } else {
            if (bytes >= 1024 * 1024) {
                Rprintf("Large allocation: %.2f MB requested\n", (double)bytes / (1024.0 * 1024.0));
            }
            std::lock_guard<std::mutex> allocator_lock(fmalloc_allocator_mutex);
            fmalloc_set_target(runtime->info);
            if (bytes > 0) {
                fmalloc_clear_last_error();
                mem = fmalloc(bytes);
                saved_errno = errno;
                if (mem && (zero_initialize || type == STRSXP)) {
                    memset(mem, 0, bytes);
                }
            }
            if (bytes == 0 || mem) {
                if (runtime->mode == FM_MODE_PERSISTENT) {
                    rfm_catalog_record *record = create_catalog_record_locked(runtime, type, length, mem, bytes);
                    saved_errno = errno;
                    if (!record) {
                        catalog_failed = true;
                        if (mem) {
                            dlfree(mem);
                            mem = nullptr;
                        }
                    } else {
                        vec->catalog_offset = pointer_offset(runtime, record);
                        vec->generation = record->generation;
                    }
                }
                if (!catalog_failed) {
                    runtime->live_vectors++;
                }
            }
        }
    }

    if (runtime_closed) {
        delete vec;
        Rf_error("fmalloc runtime is closed");
        return nullptr;
    }
    if ((bytes > 0 && !mem) || catalog_failed) {
        const char *last_error = fmalloc_get_last_error();
        delete vec;
        const char *what = catalog_failed ? "catalog record" : "payload";
        if (last_error) {
            Rf_error("fmalloc failed to allocate %s for %zu-byte vector: %s (errno: %d)",
                     what, bytes, last_error, saved_errno);
        } else {
            Rf_error("fmalloc failed to allocate %s for %zu-byte vector (errno: %d)",
                     what, bytes, saved_errno);
        }
        return nullptr;
    }

    vec->data = mem;
    if (bytes >= 1024 * 1024) {
        Rprintf("SUCCESS: fmalloc allocated %zu bytes\n", bytes);
    }
    return vec;
}

static void free_string_payloads_generic(fm_vector *vec)
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

static bool mark_catalog_record_tombstone(fm_runtime *runtime, uint64_t catalog_offset)
{
    if (!runtime || !runtime->info || catalog_offset == 0) {
        return false;
    }

    rfm_catalog_record *record = catalog_record_from_offset(runtime, catalog_offset);
    if (!record || record->magic != RFM_CATALOG_MAGIC || record->version != RFM_CATALOG_VERSION) {
        return false;
    }

    record->state = RFM_CATALOG_STATE_TOMBSTONE;
    return true;
}

static void adjust_parent_refs(fm_vector *child, int delta)
{
    if (!child || delta == 0) {
        return;
    }
    if (delta > 0) {
        child->parent_refs++;
        return;
    }
    if (child->parent_refs > 0) {
        child->parent_refs--;
    }
}

static void decrement_pointer_children(fm_vector *vec)
{
    if (!vec || !is_pointer_vector_type(vec->type) || vec->refs == R_NilValue) {
        return;
    }

    R_xlen_t len = XLENGTH(vec->refs);
    for (R_xlen_t i = 0; i < len; i++) {
        fm_vector *child = maybe_vector_from_altrep(VECTOR_ELT(vec->refs, i));
        if (child) {
            adjust_parent_refs(child, -1);
        }
    }
}

static bool destroy_fm_vector_impl(fm_vector *vec, bool free_payload, bool mark_catalog)
{
    if (!vec) {
        return false;
    }

    fm_runtime *runtime = vec->runtime;
    if (!runtime) {
        return false;
    }

    bool missing_info = false;
    struct fm_info *info_to_destroy = nullptr;
    fm_runtime *runtime_to_destroy = nullptr;

    {
        std::lock_guard<std::mutex> runtime_lock(runtime->mutex);

        if (mark_catalog && runtime->mode == FM_MODE_PERSISTENT && vec->catalog_offset != 0 &&
            runtime->info) {
            mark_catalog_record_tombstone(runtime, vec->catalog_offset);
        }

        if (free_payload && vec->data) {
            if (!runtime->info) {
                missing_info = true;
            } else {
                std::lock_guard<std::mutex> allocator_lock(fmalloc_allocator_mutex);
                fmalloc_set_target(runtime->info);
                if (vec->type == STRSXP) {
                    free_string_payloads_generic(vec);
                }
                dlfree(vec->data);
                vec->data = nullptr;
            }
        }

        if (is_pointer_vector_type(vec->type)) {
            decrement_pointer_children(vec);
        }

        if (runtime->live_vectors > 0) {
            runtime->live_vectors--;
        }
        if (runtime->close_pending && !runtime->close_requested && runtime->live_vectors == 0) {
            runtime->close_requested = true;
        }
        runtime_to_destroy = detach_runtime_if_ready(runtime, &info_to_destroy);
    }

    if (missing_info) {
        Rf_warning("fmalloc runtime was already destroyed before freeing %p", vec->data);
    }
    if (runtime_to_destroy) {
        destroy_runtime_native(runtime_to_destroy, info_to_destroy);
    }

    vec->runtime = nullptr;
    vec->refs = R_NilValue;
    vec->data = nullptr;
    vec->catalog_offset = 0;
    vec->generation = 0;
    delete vec;
    return true;
}

static bool destroy_fm_vector(fm_vector *vec, bool unsafe, bool mark_catalog)
{
    if (!vec) {
        return false;
    }
    if (vec->parent_refs > 0) {
        Rf_error("cannot destroy fmalloc vector: %llu parent reference(s) still held",
                 static_cast<unsigned long long>(vec->parent_refs));
        return false;
    }

    bool free_payload = vec->runtime != nullptr &&
        (vec->runtime->mode == FM_MODE_SCRATCH ||
         (unsafe && vec->runtime->mode == FM_MODE_PERSISTENT));
    return destroy_fm_vector_impl(vec, free_payload, mark_catalog);
}

static bool release_fm_vector(fm_vector *vec)
{
    if (!vec) {
        return false;
    }

    bool free_payload = false;
    if (vec->runtime && vec->runtime->mode == FM_MODE_SCRATCH) {
        free_payload = true;
    }
    return destroy_fm_vector_impl(vec, free_payload, false);
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

static fm_vector *maybe_vector_from_altrep(SEXP x)
{
    if (!ALTREP(x)) {
        return nullptr;
    }
    SEXP vector_xptr = R_altrep_data1(x);
    if (TYPEOF(vector_xptr) != EXTPTRSXP || R_ExternalPtrTag(vector_xptr) != fmalloc_vector_tag) {
        return nullptr;
    }
    return static_cast<fm_vector *>(R_ExternalPtrAddr(vector_xptr));
}

static fm_vector *vector_from_altrep(SEXP x)
{
    fm_vector *vec = maybe_vector_from_altrep(x);
    if (!vec) {
        Rf_error("corrupt or closed fmalloc ALTREP vector handle");
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

static bool offset_range_in_file(fm_runtime *runtime, uint64_t offset, uint64_t nbytes)
{
    return runtime && runtime->info && offset < runtime->info->len &&
           nbytes <= runtime->info->len - offset;
}

static uint32_t catalog_flags_for_type(SEXPTYPE type)
{
    uint32_t flags = 0;
    if (!is_pointer_vector_type(type)) {
        flags |= RFM_CATALOG_FLAG_RECOVERABLE;
    }
    if (type == STRSXP) {
        flags |= RFM_CATALOG_FLAG_STRING_PAYLOADS;
    }
    if (is_pointer_vector_type(type)) {
        flags |= RFM_CATALOG_FLAG_POINTER_CONTAINER;
    }
    return flags;
}

static const char *catalog_state_label(uint32_t state)
{
    switch (state) {
    case RFM_CATALOG_STATE_IN_PROGRESS:
        return "in_progress";
    case RFM_CATALOG_STATE_COMMITTED:
        return "committed";
    case RFM_CATALOG_STATE_TOMBSTONE:
        return "tombstone";
    default:
        return "unknown";
    }
}

static rfm_catalog_record *catalog_record_from_offset(fm_runtime *runtime, uint64_t offset)
{
    if (!offset_range_in_file(runtime, offset, sizeof(rfm_catalog_record))) {
        return nullptr;
    }
    return reinterpret_cast<rfm_catalog_record *>(static_cast<char *>(runtime->info->mem) + offset);
}

static rfm_catalog_record *create_catalog_record_locked(fm_runtime *runtime, SEXPTYPE type,
                                                        R_xlen_t length, void *payload,
                                                        size_t bytes)
{
    fmalloc_clear_last_error();
    void *record_mem = fmalloc(sizeof(rfm_catalog_record));
    if (!record_mem) {
        return nullptr;
    }

    rfm_app_root *root = app_root(runtime->info);
    rfm_catalog_record *record = static_cast<rfm_catalog_record *>(record_mem);
    memset(record, 0, sizeof(*record));

    uint64_t generation = root->catalog_epoch + 1;
    if (generation == 0) {
        generation = 1;
    }

    record->magic = RFM_CATALOG_MAGIC;
    record->version = RFM_CATALOG_VERSION;
    record->state = RFM_CATALOG_STATE_IN_PROGRESS;
    record->next_offset = root->catalog_head_offset;
    record->generation = generation;
    record->sexptype = (int32_t)type;
    record->flags = catalog_flags_for_type(type);
    record->length = (uint64_t)length;
    record->payload_offset = pointer_offset(runtime, payload);
    record->payload_nbytes = (uint64_t)bytes;

    root->catalog_epoch = generation;
    root->catalog_head_offset = pointer_offset(runtime, record_mem);
    root->catalog_count++;
    record->state = RFM_CATALOG_STATE_COMMITTED;
    return record;
}

static const char *validate_character_entries(fm_runtime *runtime, uint64_t payload_offset,
                                               R_xlen_t length)
{
    if (length == 0) {
        return nullptr;
    }
    if (!offset_range_in_file(runtime, payload_offset,
                              (uint64_t)length * sizeof(fm_string_entry))) {
        return "cataloged character entry table points outside the backing file";
    }

    fm_string_entry *entries = reinterpret_cast<fm_string_entry *>(
        static_cast<char *>(runtime->info->mem) + payload_offset);
    for (R_xlen_t i = 0; i < length; i++) {
        if ((entries[i].flags & FM_STRING_FLAG_NA) != 0 || entries[i].nbytes == 0) {
            continue;
        }
        if (!offset_range_in_file(runtime, entries[i].offset, entries[i].nbytes + 1)) {
            return "cataloged character string bytes point outside the backing file";
        }
    }
    return nullptr;
}

static const char *validate_catalog_record_for_ref(fm_runtime *runtime, uint64_t catalog_offset,
                                                   uint64_t generation, SEXPTYPE type,
                                                   R_xlen_t length, uint64_t payload_offset,
                                                   size_t bytes)
{
    rfm_catalog_record *record = catalog_record_from_offset(runtime, catalog_offset);
    if (!record) {
        return "serialized fmalloc catalog record points outside the backing file";
    }
    if (record->magic != RFM_CATALOG_MAGIC || record->version != RFM_CATALOG_VERSION) {
        return "serialized fmalloc catalog record has invalid header";
    }
    if (record->state != RFM_CATALOG_STATE_COMMITTED) {
        return "serialized fmalloc catalog record is not committed";
    }
    if (record->generation != generation) {
        return "serialized fmalloc catalog generation does not match";
    }
    if ((SEXPTYPE)record->sexptype != type || record->length != (uint64_t)length ||
        record->payload_offset != payload_offset || record->payload_nbytes != (uint64_t)bytes) {
        return "serialized fmalloc reference does not match its catalog record";
    }
    if ((record->flags & RFM_CATALOG_FLAG_RECOVERABLE) == 0 || is_pointer_vector_type(type)) {
        return "serialized fmalloc catalog record is not recoverable";
    }
    if (bytes > 0 && !offset_range_in_file(runtime, payload_offset, bytes)) {
        return "serialized fmalloc vector points outside the backing file";
    }
    if (type == STRSXP) {
        return validate_character_entries(runtime, payload_offset, length);
    }
    return nullptr;
}

static fm_vector *persistent_vector_from_offset(fm_runtime *runtime, SEXPTYPE type,
                                                R_xlen_t length, uint64_t offset,
                                                size_t bytes, uint64_t catalog_offset,
                                                uint64_t generation)
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
    vec->catalog_offset = catalog_offset;
    vec->generation = generation;
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

static void validate_pointer_element(fm_vector *vec, SEXP value)
{
    if (value == R_NilValue) {
        return;
    }

    fm_vector *child = maybe_vector_from_altrep(value);
    if (!child) {
        Rf_error("fmalloc list elements must be NULL or fmalloc-backed vectors; ordinary R objects cannot be stored");
        return;
    }
    if (child == vec) {
        Rf_error("fmalloc lists cannot contain themselves");
        return;
    }
    if (!child->runtime || !vec->runtime || child->runtime != vec->runtime) {
        Rf_error("fmalloc list elements must be allocated from the same open fmalloc runtime");
        return;
    }
}

static void set_pointer_element(fm_vector *vec, R_xlen_t i, SEXP value)
{
    validate_pointer_element(vec, value);


    fm_vector *old_child = maybe_vector_from_altrep(VECTOR_ELT(vec->refs, i));
    fm_vector *new_child = maybe_vector_from_altrep(value);

    if (old_child == new_child) {
        return;
    }

    if (old_child) {
        adjust_parent_refs(old_child, -1);
    }

    SET_VECTOR_ELT(vec->refs, i, value);

    if (new_child) {
        adjust_parent_refs(new_child, +1);
    }

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
