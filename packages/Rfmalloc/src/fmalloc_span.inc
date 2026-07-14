//==============================================================================
// Borrowed, read-only storage spans.
//
// A span lets a typed tensor borrow bytes already owned by another mapped
// container, such as one tensor inside a GGUF file. The external pointer keeps
// its owner reachable through the protected slot and keeps an Rfmalloc runtime
// reachable through an attribute. Rfmalloc owns neither the bytes nor their
// layout; it only consumes the span through registered codecs and allocates
// results in the attached runtime.
//==============================================================================

static SEXP fmalloc_storage_span_tag = R_NilValue;
static SEXP fmalloc_storage_runtime_symbol = R_NilValue;

struct rfm_storage_span {
    const void *data;
    size_t nbytes;
};

static void rfm_storage_span_finalizer(SEXP x)
{
    rfm_storage_span *span = static_cast<rfm_storage_span *>(
        R_ExternalPtrAddr(x));
    if (span) {
        free(span);
        R_ClearExternalPtr(x);
    }
}

// Resolve ordinary raw memory, an fmalloc raw ALTREP, or a borrowed span.
// This never raises an R error: consumers use a nonzero return as control flow.
extern "C" int Rfmalloc_storage_data(SEXP object, const void **data,
                                      size_t *nbytes, SEXP *runtime_out)
{
    if (!data || !nbytes) {
        return -1;
    }
    *data = nullptr;
    *nbytes = 0;
    if (runtime_out) {
        *runtime_out = R_NilValue;
    }

    fm_vector *vec = maybe_vector_from_altrep(object);
    if (vec) {
        if (vec->type != RAWSXP || !vec->runtime || !vec->runtime->info) {
            return -1;
        }
        *data = vector_data_or_dummy(vec);
        *nbytes = vec->bytes;
        return 0;
    }

    if (TYPEOF(object) == EXTPTRSXP &&
        R_ExternalPtrTag(object) == fmalloc_storage_span_tag) {
        rfm_storage_span *span = static_cast<rfm_storage_span *>(
            R_ExternalPtrAddr(object));
        SEXP runtime = Rf_getAttrib(object, fmalloc_storage_runtime_symbol);
        if (!span || TYPEOF(runtime) != EXTPTRSXP ||
            R_ExternalPtrTag(runtime) != fmalloc_runtime_tag ||
            !R_ExternalPtrAddr(runtime)) {
            return -1;
        }
        fm_runtime *rt = static_cast<fm_runtime *>(R_ExternalPtrAddr(runtime));
        if (!rt->info || (span->nbytes && !span->data)) {
            return -1;
        }
        *data = span->data;
        *nbytes = span->nbytes;
        if (runtime_out) {
            *runtime_out = runtime;
        }
        return 0;
    }

    // Plain raw buffers remain valid inputs to consumers such as Rllm's
    // synthetic model tests, but cannot back an fmalloc_tensor because they
    // carry no result runtime.
    if (TYPEOF(object) == RAWSXP) {
        *data = RAW(object);
        *nbytes = (size_t)XLENGTH(object);
        return 0;
    }
    return -1;
}

extern "C" SEXP Rfmalloc_storage_view(SEXP owner, SEXP runtime_xptr,
                                       const void *data, size_t nbytes)
{
    if (runtime_xptr == R_NilValue) {
        runtime_xptr = default_runtime_xptr;
    }
    fm_runtime *runtime = runtime_from_xptr(runtime_xptr);
    if (!runtime || !runtime->info) {
        Rf_error("fmalloc runtime is closed");
    }
    if (nbytes && !data) {
        Rf_error("storage view has a null data pointer");
    }

    rfm_storage_span *span = static_cast<rfm_storage_span *>(
        calloc(1, sizeof(*span)));
    if (!span) {
        Rf_error("failed to allocate storage view");
    }
    span->data = data;
    span->nbytes = nbytes;

    SEXP runtime_ref = PROTECT(make_runtime_xptr(runtime, true, false));
    SEXP ans = PROTECT(R_MakeExternalPtr(span, fmalloc_storage_span_tag, owner));
    R_RegisterCFinalizerEx(ans, rfm_storage_span_finalizer, TRUE);
    Rf_setAttrib(ans, fmalloc_storage_runtime_symbol, runtime_ref);
    SEXP cls = PROTECT(Rf_mkString("fmalloc_storage_view"));
    Rf_setAttrib(ans, R_ClassSymbol, cls);
    UNPROTECT(3);
    return ans;
}

extern "C" SEXP rfm_tensor_payload_nbytes_impl(SEXP payload)
{
    const void *data;
    size_t nbytes;
    SEXP runtime;
    if (Rfmalloc_storage_data(payload, &data, &nbytes, &runtime) != 0 ||
        (runtime == R_NilValue && !maybe_vector_from_altrep(payload))) {
        return R_NilValue;
    }
    return Rf_ScalarReal((double)nbytes);
}

extern "C" int Rfmalloc_storage_advise(SEXP object, size_t offset,
                                        size_t nbytes, int advice)
{
    const void *data;
    size_t total;
    if (Rfmalloc_storage_data(object, &data, &total, nullptr) != 0 ||
        offset > total || nbytes > total - offset || advice < 0 || advice > 2) {
        return -1;
    }
    if (nbytes == 0) {
        return 0;
    }
    ooc_advice_t native = advice == 0 ? OOC_SEQUENTIAL
                            : advice == 1 ? OOC_DONTNEED
                                          : OOC_WILLNEED;
    ooc_advise(const_cast<uint8_t *>(
                   static_cast<const uint8_t *>(data)) + offset,
               nbytes, native);
    return 0;
}

extern "C" SEXP rfm_storage_advise_impl(SEXP object, SEXP offset_sexp,
                                         SEXP nbytes_sexp, SEXP advice_sexp)
{
    const double offset_d = Rf_asReal(offset_sexp);
    const double nbytes_d = Rf_asReal(nbytes_sexp);
    const int advice = Rf_asInteger(advice_sexp);
    if (!R_FINITE(offset_d) || !R_FINITE(nbytes_d) || offset_d < 0 ||
        nbytes_d < 0 || offset_d != std::floor(offset_d) ||
        nbytes_d != std::floor(nbytes_d) || offset_d >= (double)SIZE_MAX ||
        nbytes_d >= (double)SIZE_MAX ||
        Rfmalloc_storage_advise(object, (size_t)offset_d, (size_t)nbytes_d,
                                advice) != 0) {
        Rf_error("invalid storage advice range or payload");
    }
    return object;
}
