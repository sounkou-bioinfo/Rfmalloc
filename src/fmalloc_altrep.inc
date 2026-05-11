//==============================================================================
// ALTREP methods
//==============================================================================

static bool fmalloc_call_is_bracket2(SEXP call);
static SEXP fmalloc_altvec_single_bracket2_extract(SEXP x, fm_vector *src, R_xlen_t src_i);

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
    fm_vector *new_vec = allocate_fm_vector(old_vec->runtime, old_vec->type, old_vec->len, false, false);
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

static SEXP scalar_u64_hex(uint64_t value)
{
    char buf[17];
    std::snprintf(buf, sizeof(buf), "%016llx", (unsigned long long)value);
    return Rf_mkString(buf);
}

static bool parse_u64_state_hex_or_real(SEXP value_sexp, uint64_t *out)
{
    if (TYPEOF(value_sexp) == STRSXP && LENGTH(value_sexp) == 1) {
        const char *value_text = CHAR(STRING_ELT(value_sexp, 0));
        if (!value_text || std::strlen(value_text) != 16) {
            return false;
        }
        char *end = nullptr;
        unsigned long long parsed = std::strtoull(value_text, &end, 16);
        if (!end || *end != '\0') {
            return false;
        }
        *out = (uint64_t)parsed;
        return true;
    }

    if (TYPEOF(value_sexp) == REALSXP && LENGTH(value_sexp) == 1) {
        double value = REAL(value_sexp)[0];
        if (!std::isfinite(value) || value < 0 ||
            value > (double)std::numeric_limits<uint64_t>::max()) {
            return false;
        }
        uint64_t parsed = static_cast<uint64_t>(value);
        if ((double)parsed != value) {
            return false;
        }
        *out = parsed;
        return true;
    }

    return false;
}

static SEXP make_persistent_ref_state(fm_vector *vec)
{
    uint64_t payload_offset = pointer_offset(vec->runtime, vec->data);
    const char *catalog_error = validate_catalog_record_for_ref(
        vec->runtime, vec->catalog_offset, vec->generation, vec->type,
        vec->len, payload_offset, vec->bytes);
    if (catalog_error) {
        Rf_error("cannot serialize fmalloc vector by reference: %s", catalog_error);
        return R_NilValue;
    }

    SEXP state = PROTECT(Rf_allocVector(VECSXP, 10));
    SEXP names = PROTECT(Rf_allocVector(STRSXP, 10));
    const char *name_values[] = {
        "format", "path", "uuid", "type", "length", "offset", "nbytes", "version",
        "catalog_offset", "generation"
    };
    for (int i = 0; i < 10; i++) {
        SET_STRING_ELT(names, i, Rf_mkChar(name_values[i]));
    }
    Rf_setAttrib(state, R_NamesSymbol, names);

    SET_VECTOR_ELT(state, 0, Rf_mkString("RfmallocRef"));
    SET_VECTOR_ELT(state, 1, Rf_mkString(vec->runtime->filepath.c_str()));
    std::string uuid = uuid_string(vec->runtime->file_uuid_hi, vec->runtime->file_uuid_lo);
    SET_VECTOR_ELT(state, 2, Rf_mkString(uuid.c_str()));
    SET_VECTOR_ELT(state, 3, Rf_ScalarInteger((int)vec->type));
    SET_VECTOR_ELT(state, 4, Rf_ScalarReal((double)vec->len));
    SET_VECTOR_ELT(state, 5, scalar_u64_hex(payload_offset));
    SET_VECTOR_ELT(state, 6, scalar_u64_hex(static_cast<uint64_t>(vec->bytes)));
    SET_VECTOR_ELT(state, 7, Rf_ScalarInteger(2));
    SET_VECTOR_ELT(state, 8, scalar_u64_hex(vec->catalog_offset));
    SET_VECTOR_ELT(state, 9, scalar_u64_hex(vec->generation));

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

static bool is_persistent_list_state(SEXP state)
{
    if (TYPEOF(state) != VECSXP) {
        return false;
    }
    SEXP format = list_get(state, "format");
    return TYPEOF(format) == STRSXP && LENGTH(format) == 1 &&
           strcmp(CHAR(STRING_ELT(format, 0)), "RfmallocListRef") == 0;
}

static SEXP fmalloc_altrep_unserialize(SEXP cls, SEXP state);
static SEXP fmalloc_altrep_unserialize_with_runtime(SEXP cls, SEXP state, fm_runtime *runtime);

static SEXP make_persistent_list_state_recursive(fm_vector *vec, std::vector<fm_vector *> &stack)
{
    if (!vec || vec->type != VECSXP || !vec->runtime || vec->runtime->mode != FM_MODE_PERSISTENT ||
        TYPEOF(vec->refs) != VECSXP || XLENGTH(vec->refs) != vec->len) {
        return R_NilValue;
    }

    SEXP child_states = PROTECT(Rf_allocVector(VECSXP, vec->len));
    bool recoverable = true;

    for (R_xlen_t i = 0; i < vec->len; i++) {
        SEXP child = VECTOR_ELT(vec->refs, i);
        SEXP child_state = R_NilValue;

        if (child == R_NilValue) {
            SET_VECTOR_ELT(child_states, i, R_NilValue);
            continue;
        }

        fm_vector *child_vec = maybe_vector_from_altrep(child);
        if (!child_vec || child_vec->runtime != vec->runtime ||
            !child_vec->runtime || child_vec->runtime->mode != FM_MODE_PERSISTENT) {
            recoverable = false;
            break;
        }

        if (child_vec->type == VECSXP) {
            if (std::find(stack.begin(), stack.end(), child_vec) != stack.end()) {
                recoverable = false;
                break;
            }
            stack.push_back(child_vec);
            child_state = make_persistent_list_state_recursive(child_vec, stack);
            stack.pop_back();
            if (child_state == R_NilValue) {
                recoverable = false;
                break;
            }
        } else {
            child_state = make_persistent_ref_state(child_vec);
        }

        if (child_state == R_NilValue) {
            recoverable = false;
            break;
        }
        SET_VECTOR_ELT(child_states, i, child_state);
    }

    if (!recoverable) {
        UNPROTECT(1);
        return R_NilValue;
    }

    SEXP state = PROTECT(Rf_allocVector(VECSXP, 5));
    SEXP names = PROTECT(Rf_allocVector(STRSXP, 5));
    const char *name_values[] = {
        "format", "path", "uuid", "length", "children"
    };
    for (int i = 0; i < 5; i++) {
        SET_STRING_ELT(names, i, Rf_mkChar(name_values[i]));
    }
    Rf_setAttrib(state, R_NamesSymbol, names);

    SET_VECTOR_ELT(state, 0, Rf_mkString("RfmallocListRef"));
    SET_VECTOR_ELT(state, 1, Rf_mkString(vec->runtime->filepath.c_str()));
    std::string uuid = uuid_string(vec->runtime->file_uuid_hi, vec->runtime->file_uuid_lo);
    SET_VECTOR_ELT(state, 2, Rf_mkString(uuid.c_str()));
    SET_VECTOR_ELT(state, 3, Rf_ScalarReal((double)vec->len));
    SET_VECTOR_ELT(state, 4, child_states);

    UNPROTECT(3);
    return state;
}

static SEXP make_persistent_list_state(SEXP x)
{
    fm_vector *vec = vector_from_altrep(x);
    if (!vec || vec->runtime->mode != FM_MODE_PERSISTENT) {
        return R_NilValue;
    }

    std::vector<fm_vector *> stack;
    stack.push_back(vec);
    return make_persistent_list_state_recursive(vec, stack);
}

static SEXP restore_persistent_list_element(SEXP child_state, fm_runtime *runtime)
{
    if (child_state == R_NilValue) {
        return R_NilValue;
    }
    if (is_persistent_ref_state(child_state) || is_persistent_list_state(child_state)) {
        return fmalloc_altrep_unserialize_with_runtime(R_NilValue, child_state, runtime);
    }
    Rf_error("invalid Rfmalloc serialized list element state");
    return R_NilValue;
}

static SEXP restore_persistent_list(SEXP state, fm_runtime *runtime)
{
    SEXP path_sexp = list_get(state, "path");
    SEXP uuid_sexp = list_get(state, "uuid");
    SEXP length_sexp = list_get(state, "length");
    SEXP children_sexp = list_get(state, "children");

    if (TYPEOF(path_sexp) != STRSXP || LENGTH(path_sexp) != 1 ||
        TYPEOF(uuid_sexp) != STRSXP || LENGTH(uuid_sexp) != 1 ||
        TYPEOF(length_sexp) != REALSXP || LENGTH(length_sexp) != 1 ||
        TYPEOF(children_sexp) != VECSXP || !std::isfinite(REAL(length_sexp)[0])) {
        Rf_error("invalid Rfmalloc serialized list reference");
        return R_NilValue;
    }

    uint64_t expected_hi = 0;
    uint64_t expected_lo = 0;
    if (!parse_uuid_string(CHAR(STRING_ELT(uuid_sexp, 0)), &expected_hi, &expected_lo)) {
        Rf_error("invalid Rfmalloc serialized UUID");
        return R_NilValue;
    }

    double length_value = REAL(length_sexp)[0];
    if (!std::isfinite(length_value) || length_value < 0 ||
        length_value > (double)std::numeric_limits<R_xlen_t>::max() ||
        (R_xlen_t)length_value != length_value) {
        Rf_error("invalid Rfmalloc serialized list reference dimensions");
        return R_NilValue;
    }

    R_xlen_t len = (R_xlen_t)length_value;
    if (XLENGTH(children_sexp) != len) {
        Rf_error("invalid Rfmalloc serialized list reference children");
        return R_NilValue;
    }

    const char *path = CHAR(STRING_ELT(path_sexp, 0));
    bool opened_runtime = false;
    if (!runtime) {
        runtime = open_runtime_native(path, 32 * 1024 * 1024 + FMALLOC_OFF,
                                     FM_MODE_PERSISTENT, true);
        opened_runtime = true;
    }
    if (!runtime || runtime->file_uuid_hi != expected_hi || runtime->file_uuid_lo != expected_lo) {
        if (opened_runtime && runtime) {
            close_unreferenced_runtime(runtime);
        }
        Rf_error("Rfmalloc serialized list reference points to a different backing file UUID");
        return R_NilValue;
    }

    fm_vector *vec = allocate_fm_vector(runtime, VECSXP, len, false, false);
    SEXP ans = PROTECT(fmalloc_new_altrep(vec));
    for (R_xlen_t i = 0; i < len; i++) {
        SEXP child_state = VECTOR_ELT(children_sexp, i);
        SEXP child = restore_persistent_list_element(child_state, runtime);
        set_pointer_element(vec, i, child);
    }

    UNPROTECT(1);
    return ans;
}

static SEXP fmalloc_altrep_unserialize_with_runtime(SEXP cls, SEXP state, fm_runtime *runtime)
{
    (void)cls;
    if (is_persistent_ref_state(state)) {
        SEXP path_sexp = list_get(state, "path");
        SEXP uuid_sexp = list_get(state, "uuid");
        SEXP type_sexp = list_get(state, "type");
        SEXP length_sexp = list_get(state, "length");
        SEXP offset_sexp = list_get(state, "offset");
        SEXP nbytes_sexp = list_get(state, "nbytes");
        SEXP catalog_offset_sexp = list_get(state, "catalog_offset");
        SEXP generation_sexp = list_get(state, "generation");

        if (TYPEOF(path_sexp) != STRSXP || LENGTH(path_sexp) != 1 ||
            TYPEOF(uuid_sexp) != STRSXP || LENGTH(uuid_sexp) != 1 ||
            TYPEOF(type_sexp) != INTSXP || LENGTH(type_sexp) != 1 ||
            TYPEOF(length_sexp) != REALSXP || LENGTH(length_sexp) != 1 ||
            (TYPEOF(offset_sexp) != REALSXP && TYPEOF(offset_sexp) != STRSXP) || LENGTH(offset_sexp) != 1 ||
            (TYPEOF(nbytes_sexp) != REALSXP && TYPEOF(nbytes_sexp) != STRSXP) || LENGTH(nbytes_sexp) != 1 ||
            (TYPEOF(catalog_offset_sexp) != REALSXP && TYPEOF(catalog_offset_sexp) != STRSXP) || LENGTH(catalog_offset_sexp) != 1 ||
            (TYPEOF(generation_sexp) != REALSXP && TYPEOF(generation_sexp) != STRSXP) || LENGTH(generation_sexp) != 1) {
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
        uint64_t offset = 0;
        uint64_t nbytes_value = 0;
        uint64_t catalog_offset = 0;
        uint64_t generation = 0;

        if (!std::isfinite(length_value) || length_value < 0 ||
            length_value > (double)std::numeric_limits<R_xlen_t>::max() ||
            !parse_u64_state_hex_or_real(offset_sexp, &offset) ||
            !parse_u64_state_hex_or_real(nbytes_sexp, &nbytes_value) ||
            !parse_u64_state_hex_or_real(catalog_offset_sexp, &catalog_offset) ||
            !parse_u64_state_hex_or_real(generation_sexp, &generation) ||
            nbytes_value > (uint64_t)std::numeric_limits<size_t>::max() ||
            catalog_offset == 0 ||
            generation == 0) {
            Rf_error("invalid Rfmalloc serialized reference dimensions");
            return R_NilValue;
        }

        R_xlen_t length = (R_xlen_t)length_value;
        size_t nbytes = static_cast<size_t>(nbytes_value);
        if ((double)length != length_value || !is_supported_vector_type(type) ||
            is_pointer_vector_type(type) || nbytes != payload_size_bytes(type, length)) {
            Rf_error("invalid Rfmalloc serialized reference metadata");
            return R_NilValue;
        }

        const char *path = CHAR(STRING_ELT(path_sexp, 0));
        bool opened_runtime = false;
        if (!runtime) {
            runtime = open_runtime_native(path, 32 * 1024 * 1024 + FMALLOC_OFF,
                                         FM_MODE_PERSISTENT, true);
            opened_runtime = true;
        } else if (runtime->file_uuid_hi != expected_hi || runtime->file_uuid_lo != expected_lo) {
            Rf_error("Rfmalloc serialized reference points to a different backing file UUID");
            return R_NilValue;
        }

        if (!runtime) {
            Rf_error("Rfmalloc serialized reference points to an unreachable backing file");
            return R_NilValue;
        }

        const char *catalog_error = validate_catalog_record_for_ref(
            runtime, catalog_offset, generation, type, length, offset, nbytes);
        if (catalog_error) {
            if (opened_runtime) {
                close_unreferenced_runtime(runtime);
            }
            Rf_error("invalid Rfmalloc serialized reference: %s", catalog_error);
            return R_NilValue;
        }

        fm_vector *vec = persistent_vector_from_offset(runtime, type, length, offset, nbytes,
                                                   catalog_offset, generation);
        return fmalloc_new_altrep(vec);
    }

    if (is_persistent_list_state(state)) {
        return restore_persistent_list(state, runtime);
    }

    return state;
}

static SEXP fmalloc_altrep_serialized_state(SEXP x)
{
    fm_vector *vec = vector_from_altrep(x);
    if (vec->runtime->mode == FM_MODE_PERSISTENT && !is_pointer_vector_type(vec->type)) {
        return make_persistent_ref_state(vec);
    }
    if (vec->runtime->mode == FM_MODE_PERSISTENT && is_pointer_vector_type(vec->type)) {
        SEXP state = make_persistent_list_state(x);
        if (state != R_NilValue) {
            return state;
        }
    }

    return fmalloc_plain_vector(x);
}

static SEXP fmalloc_altrep_unserialize(SEXP cls, SEXP state)
{
    return fmalloc_altrep_unserialize_with_runtime(cls, state, nullptr);
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
    fm_vector *new_vec = allocate_fm_vector(old_vec->runtime, target_type, XLENGTH(coerced), false, false);
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
        double truncated = std::floor(value);
        if (truncated != value) {
            return false;
        }
        R_xlen_t offset = (R_xlen_t)(truncated - 1.0);
        if (offset >= 0 && offset < source_length) {
            *source_i = offset;
            return true;
        }
        return false;
    }

    return false;
}

static bool subset_index_fast_compatible(SEXP indx)
{
    if (TYPEOF(indx) != INTSXP) {
        return false;
    }

    for (R_xlen_t i = 0; i < XLENGTH(indx); i++) {
        int value = INTEGER(indx)[i];
        if (value == NA_INTEGER || value <= 0) {
            return false;
        }
    }
    return true;
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

static bool index_has_negative(SEXP indx)
{
    R_xlen_t n = XLENGTH(indx);
    if (TYPEOF(indx) == INTSXP) {
        for (R_xlen_t i = 0; i < n; i++) {
            int v = INTEGER(indx)[i];
            if (v != NA_INTEGER && v < 0) return true;
        }
    }
    if (TYPEOF(indx) == REALSXP) {
        for (R_xlen_t i = 0; i < n; i++) {
            double v = REAL(indx)[i];
            if (R_FINITE(v) && (v < 0 || floor(v) != v)) return true;
        }
    }
    return false;
}

static SEXP fmalloc_altvec_extract_subset(SEXP x, SEXP indx, SEXP call)
{
    if (TYPEOF(indx) != INTSXP && TYPEOF(indx) != REALSXP) {
        return nullptr;
    }

    if (index_has_negative(indx)) {
        return nullptr;
    }

    fm_vector *src = vector_from_altrep(x);
    const bool is_bracket2 = fmalloc_call_is_bracket2(call);

    if (is_bracket2 && XLENGTH(indx) == 1L) {
        R_xlen_t src_i = 0;
        if (!subset_index_to_offset(indx, 0, src->len, &src_i)) {
            Rf_error("subscript out of bounds");
        }
        return fmalloc_altvec_single_bracket2_extract(x, src, src_i);
    }

    if (TYPEOF(indx) == REALSXP || !subset_index_fast_compatible(indx)) {
        return nullptr;
    }

    R_xlen_t n = XLENGTH(indx);
    fm_vector *dst = allocate_fm_vector(src->runtime, src->type, n, false, false);
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

static bool fmalloc_call_is_bracket2(SEXP call)
{
    return call != R_NilValue && TYPEOF(call) == LANGSXP && CAR(call) == R_Bracket2Symbol;
}

static SEXP fmalloc_altvec_single_bracket2_extract(SEXP x, fm_vector *src, R_xlen_t src_i)
{
    switch (src->type) {
    case LGLSXP:
        return Rf_ScalarLogical(static_cast<int *>(vector_data_or_dummy(src))[src_i]);
    case INTSXP:
        return Rf_ScalarInteger(static_cast<int *>(vector_data_or_dummy(src))[src_i]);
    case REALSXP:
        return Rf_ScalarReal(static_cast<double *>(vector_data_or_dummy(src))[src_i]);
    case RAWSXP:
        return Rf_ScalarRaw(static_cast<Rbyte *>(vector_data_or_dummy(src))[src_i]);
    case CPLXSXP:
        return Rf_ScalarComplex(static_cast<Rcomplex *>(vector_data_or_dummy(src))[src_i]);
    case STRSXP:
        return string_elt_from_vec(src, src_i);
    case VECSXP:
        return VECTOR_ELT(pointer_refs_from_altrep(x), src_i);
    default:
        return Rf_ScalarLogical(NA_LOGICAL);
    }
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

struct catalog_snapshot {
    uint64_t record_offset;
    uint64_t generation;
    uint32_t state;
    SEXPTYPE type;
    uint32_t flags;
    uint64_t length;
    uint64_t payload_offset;
    uint64_t payload_nbytes;
};

static std::vector<catalog_snapshot> snapshot_catalog(fm_runtime *runtime)
{
    std::vector<catalog_snapshot> records;
    const char *error_msg = nullptr;
    if (!runtime || !runtime->info) {
        Rf_error("fmalloc runtime is closed");
        return records;
    }

    {
        std::lock_guard<std::mutex> runtime_lock(runtime->mutex);
        if (!runtime->info) {
            error_msg = "fmalloc runtime is closed";
        } else {
            rfm_app_root *root = app_root(runtime->info);
            uint64_t offset = root->catalog_head_offset;
            uint64_t max_records = runtime->info->len / sizeof(rfm_catalog_record) + 1;
            if (root->catalog_count > 0 && root->catalog_count + 1024 < max_records) {
                max_records = root->catalog_count + 1024;
            }

            for (uint64_t seen = 0; offset != 0; seen++) {
                if (seen >= max_records) {
                    error_msg = "fmalloc allocation catalog appears to contain a cycle";
                    break;
                }
                rfm_catalog_record *record = catalog_record_from_offset(runtime, offset);
                if (!record || record->magic != RFM_CATALOG_MAGIC ||
                    record->version != RFM_CATALOG_VERSION) {
                    error_msg = "fmalloc allocation catalog is corrupt";
                    break;
                }

                catalog_snapshot snapshot;
                snapshot.record_offset = offset;
                snapshot.generation = record->generation;
                snapshot.state = record->state;
                snapshot.type = (SEXPTYPE)record->sexptype;
                snapshot.flags = record->flags;
                snapshot.length = record->length;
                snapshot.payload_offset = record->payload_offset;
                snapshot.payload_nbytes = record->payload_nbytes;
                records.push_back(snapshot);
                offset = record->next_offset;
            }
        }
    }

    if (error_msg) {
        Rf_error("%s", error_msg);
    }
    return records;
}

static SEXP make_catalog_data_frame(const std::vector<catalog_snapshot> &records)
{
    if (records.size() > (size_t)std::numeric_limits<int>::max()) {
        Rf_error("too many fmalloc catalog records to return as a data frame");
        return R_NilValue;
    }

    R_xlen_t n = (R_xlen_t)records.size();
    const int ncols = 9;
    SEXP ans = PROTECT(Rf_allocVector(VECSXP, ncols));
    SEXP names = PROTECT(Rf_allocVector(STRSXP, ncols));
    const char *name_values[] = {
        "record_offset", "generation", "state", "type", "length",
        "payload_offset", "payload_nbytes", "flags", "recoverable"
    };
    for (int i = 0; i < ncols; i++) {
        SET_STRING_ELT(names, i, Rf_mkChar(name_values[i]));
    }
    Rf_setAttrib(ans, R_NamesSymbol, names);

    SEXP record_offset = PROTECT(Rf_allocVector(REALSXP, n));
    SEXP generation = PROTECT(Rf_allocVector(REALSXP, n));
    SEXP state = PROTECT(Rf_allocVector(STRSXP, n));
    SEXP type = PROTECT(Rf_allocVector(STRSXP, n));
    SEXP length = PROTECT(Rf_allocVector(REALSXP, n));
    SEXP payload_offset = PROTECT(Rf_allocVector(REALSXP, n));
    SEXP payload_nbytes = PROTECT(Rf_allocVector(REALSXP, n));
    SEXP flags = PROTECT(Rf_allocVector(INTSXP, n));
    SEXP recoverable = PROTECT(Rf_allocVector(LGLSXP, n));

    for (R_xlen_t i = 0; i < n; i++) {
        const catalog_snapshot &record = records[(size_t)i];
        REAL(record_offset)[i] = (double)record.record_offset;
        REAL(generation)[i] = (double)record.generation;
        SET_STRING_ELT(state, i, Rf_mkChar(catalog_state_label(record.state)));
        SET_STRING_ELT(type, i, Rf_mkChar(type_label(record.type)));
        REAL(length)[i] = (double)record.length;
        REAL(payload_offset)[i] = (double)record.payload_offset;
        REAL(payload_nbytes)[i] = (double)record.payload_nbytes;
        INTEGER(flags)[i] = (int)record.flags;
        LOGICAL(recoverable)[i] = (record.flags & RFM_CATALOG_FLAG_RECOVERABLE) != 0;
    }

    SET_VECTOR_ELT(ans, 0, record_offset);
    SET_VECTOR_ELT(ans, 1, generation);
    SET_VECTOR_ELT(ans, 2, state);
    SET_VECTOR_ELT(ans, 3, type);
    SET_VECTOR_ELT(ans, 4, length);
    SET_VECTOR_ELT(ans, 5, payload_offset);
    SET_VECTOR_ELT(ans, 6, payload_nbytes);
    SET_VECTOR_ELT(ans, 7, flags);
    SET_VECTOR_ELT(ans, 8, recoverable);

    SEXP row_names = PROTECT(Rf_allocVector(INTSXP, 2));
    INTEGER(row_names)[0] = NA_INTEGER;
    INTEGER(row_names)[1] = -(int)n;
    Rf_setAttrib(ans, R_RowNamesSymbol, row_names);
    Rf_setAttrib(ans, R_ClassSymbol, Rf_mkString("data.frame"));

    UNPROTECT(12);
    return ans;
}

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
