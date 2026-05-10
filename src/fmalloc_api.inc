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

SEXP list_fmalloc_allocations_impl(SEXP runtime_xptr)
{
    fm_runtime *runtime = runtime_from_xptr(runtime_xptr);
    std::vector<catalog_snapshot> records = snapshot_catalog(runtime);
    return make_catalog_data_frame(records);
}

SEXP destroy_fmalloc_vector_impl(SEXP vector_x, SEXP unsafe_sexp)
{
    if (!ALTREP(vector_x)) {
        Rf_error("destroy_fmalloc_vector expects an fmalloc ALTREP vector");
        return R_NilValue;
    }

    if (TYPEOF(unsafe_sexp) != LGLSXP || LENGTH(unsafe_sexp) != 1) {
        Rf_error("unsafe must be a single logical value");
        return R_NilValue;
    }
    int unsafe_i = Rf_asLogical(unsafe_sexp);
    if (unsafe_i == NA_LOGICAL) {
        Rf_error("unsafe must be TRUE or FALSE");
        return R_NilValue;
    }

    SEXP vector_xptr = R_altrep_data1(vector_x);
    if (TYPEOF(vector_xptr) != EXTPTRSXP || R_ExternalPtrTag(vector_xptr) != fmalloc_vector_tag) {
        Rf_error("x is not an fmalloc ALTREP vector");
        return R_NilValue;
    }

    fm_vector *vec = static_cast<fm_vector *>(R_ExternalPtrAddr(vector_xptr));
    if (!vec) {
        return Rf_ScalarLogical(FALSE);
    }

    bool unsafe = unsafe_i == 1;
    bool destroyed = destroy_fm_vector(vec, unsafe, true);
    R_ClearExternalPtr(vector_xptr);
    R_SetExternalPtrProtected(vector_xptr, R_NilValue);
    return Rf_ScalarLogical(destroyed);
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

int Rfmalloc_api_version(void)
{
    return 1;
}

SEXP Rfmalloc_default_runtime(void)
{
    return default_runtime_xptr;
}

SEXP Rfmalloc_open_runtime(const char *filepath, double size_gb, const char *mode)
{
    if (!filepath || filepath[0] == '\0') {
        Rf_error("filepath must be a non-empty C string");
        return R_NilValue;
    }
    if (!mode || mode[0] == '\0') {
        mode = "persistent";
    }

    int nprotect = 0;
    SEXP filepath_sexp = PROTECT(Rf_mkString(filepath));
    nprotect++;
    SEXP mode_sexp = PROTECT(Rf_mkString(mode));
    nprotect++;
    SEXP size_gb_sexp = R_NilValue;
    if (std::isfinite(size_gb) && size_gb > 0) {
        size_gb_sexp = PROTECT(Rf_ScalarReal(size_gb));
        nprotect++;
    }

    SEXP ans = PROTECT(open_fmalloc_impl(filepath_sexp, size_gb_sexp, mode_sexp));
    nprotect++;
    UNPROTECT(nprotect);
    return ans;
}

SEXP Rfmalloc_create_vector(SEXP runtime_xptr, int sexptype, R_xlen_t length)
{
    if (length < 0) {
        Rf_error("length must be non-negative");
        return R_NilValue;
    }

    SEXPTYPE type = (SEXPTYPE)sexptype;
    if (!is_supported_vector_type(type)) {
        Rf_error("Unsupported vector type: %d", type);
        return R_NilValue;
    }

    fm_runtime *runtime = runtime_from_xptr(runtime_xptr);
    fm_vector *vec = allocate_fm_vector(runtime, type, length, true);
    SEXP ans = PROTECT(fmalloc_new_altrep(vec));
    UNPROTECT(1);
    return ans;
}

void Rfmalloc_cleanup_runtime(SEXP runtime_xptr)
{
    if (Rf_isNull(runtime_xptr)) {
        return;
    }
    if (TYPEOF(runtime_xptr) != EXTPTRSXP || R_ExternalPtrTag(runtime_xptr) != fmalloc_runtime_tag) {
        Rf_error("invalid fmalloc runtime");
        return;
    }
    request_runtime_close(runtime_xptr, true);
}

SEXP Rfmalloc_list_allocations(SEXP runtime_xptr)
{
    fm_runtime *runtime = runtime_from_xptr(runtime_xptr);
    std::vector<catalog_snapshot> records = snapshot_catalog(runtime);
    return make_catalog_data_frame(records);
}

static void register_c_callables(void)
{
    R_RegisterCCallable("Rfmalloc", "Rfmalloc_api_version", (DL_FUNC)Rfmalloc_api_version);
    R_RegisterCCallable("Rfmalloc", "Rfmalloc_default_runtime", (DL_FUNC)Rfmalloc_default_runtime);
    R_RegisterCCallable("Rfmalloc", "Rfmalloc_open_runtime", (DL_FUNC)Rfmalloc_open_runtime);
    R_RegisterCCallable("Rfmalloc", "Rfmalloc_create_vector", (DL_FUNC)Rfmalloc_create_vector);
    R_RegisterCCallable("Rfmalloc", "Rfmalloc_cleanup_runtime", (DL_FUNC)Rfmalloc_cleanup_runtime);
    R_RegisterCCallable("Rfmalloc", "Rfmalloc_list_allocations", (DL_FUNC)Rfmalloc_list_allocations);
}

static const R_CallMethodDef CallEntries[] = {
    {"open_fmalloc_impl", (DL_FUNC)&open_fmalloc_impl, 3},
    {"init_fmalloc_impl", (DL_FUNC)&init_fmalloc_impl, 3},
    {"create_fmalloc_vector_impl", (DL_FUNC)&create_fmalloc_vector_impl, 3},
    {"list_fmalloc_allocations_impl", (DL_FUNC)&list_fmalloc_allocations_impl, 1},
    {"destroy_fmalloc_vector_impl", (DL_FUNC)&destroy_fmalloc_vector_impl, 2},
    {"cleanup_fmalloc_impl", (DL_FUNC)&cleanup_fmalloc_impl, 1},
    {nullptr, nullptr, 0}
};

void R_init_Rfmalloc(DllInfo *dll)
{
    fmalloc_runtime_tag = Rf_install("Rfmalloc.runtime");
    fmalloc_vector_tag = Rf_install("Rfmalloc.vector");
    register_fmalloc_altrep_classes(dll);
    R_registerRoutines(dll, nullptr, CallEntries, nullptr, nullptr);
    register_c_callables();
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
