//==============================================================================
// Record-panel transfer context.
//
// Sources describe semantic records; Rfmalloc owns their destination layout.
// The same context writes plain f64 storage, fixed-point dosage, packed
// hardcalls, or locus-major haplotype bits. A reader therefore does not branch
// on "compressed" versus "uncompressed" allocation and never reaches into a
// codec header.
//==============================================================================

#define RFMALLOC_BUFFER_I32 1
#define RFMALLOC_BUFFER_F64 2
#define RFMALLOC_BUFFER_PACKED_BITS 3

#define RFM_BUFFER_BED 1
#define RFM_BUFFER_DOSAGE 2
#define RFM_BUFFER_HAPLOTYPE 3
#define RFM_BUFFER_PLAIN_F64 4

struct Rfmalloc_buffer_context {
    SEXP payload;
    R_xlen_t n_item;
    R_xlen_t n_record;
    int storage;
};

static void rfm_buffer_discard_payload(SEXP payload)
{
    SEXP object = payload;
    while (ALTREP(object)) {
        SEXP data1 = R_altrep_data1(object);
        if (TYPEOF(data1) == EXTPTRSXP &&
            R_ExternalPtrTag(data1) == fmalloc_vector_tag) {
            fm_vector *vec = static_cast<fm_vector *>(R_ExternalPtrAddr(data1));
            if (vec) {
                destroy_fm_vector_impl(vec, true, true);
                R_ClearExternalPtr(data1);
            }
            return;
        }
        if (TYPEOF(data1) != TYPEOF(object)) {
            return;
        }
        object = data1;
    }
}

static bool rfm_buffer_record_bytes(R_xlen_t n_item, size_t item_size,
                                    size_t *out)
{
    if ((uint64_t)n_item > SIZE_MAX / item_size) {
        return false;
    }
    *out = (size_t)n_item * item_size;
    return true;
}

extern "C" Rfmalloc_buffer_context *
Rfmalloc_buffer_open(SEXP runtime_xptr, const char *storage,
                     R_xlen_t n_item, R_xlen_t n_record)
{
    if (!storage || n_item <= 0 || n_record <= 0) {
        return nullptr;
    }

    int kind;
    SEXP payload;
    if (strcmp(storage, "bed") == 0) {
        kind = RFM_BUFFER_BED;
        payload = PROTECT(Rfmalloc_bed_create(runtime_xptr, n_item, n_record));
    } else if (strcmp(storage, "dosage") == 0) {
        kind = RFM_BUFFER_DOSAGE;
        payload = PROTECT(Rfmalloc_dosage_create(runtime_xptr, n_item, n_record));
    } else if (strcmp(storage, "haplotype") == 0) {
        kind = RFM_BUFFER_HAPLOTYPE;
        payload = PROTECT(Rfmalloc_hap_create(runtime_xptr, n_record, n_item));
    } else if (strcmp(storage, "f64") == 0) {
        if (n_item > std::numeric_limits<R_xlen_t>::max() / n_record) {
            return nullptr;
        }
        kind = RFM_BUFFER_PLAIN_F64;
        fm_runtime *runtime = runtime_from_xptr(runtime_xptr);
        if (!runtime || !runtime->info) {
            Rf_error("fmalloc runtime is closed");
        }
        fm_vector *vec = allocate_fm_vector(runtime, REALSXP,
                                            n_item * n_record, true, false);
        payload = PROTECT(fmalloc_new_altrep(vec));
    } else {
        return nullptr;
    }

    Rfmalloc_buffer_context *ctx = static_cast<Rfmalloc_buffer_context *>(
        calloc(1, sizeof(*ctx)));
    if (!ctx) {
        rfm_buffer_discard_payload(payload);
        UNPROTECT(1);
        return nullptr;
    }
    ctx->payload = payload;
    ctx->n_item = n_item;
    ctx->n_record = n_record;
    ctx->storage = kind;
    R_PreserveObject(payload);
    UNPROTECT(1);
    return ctx;
}

extern "C" int
Rfmalloc_buffer_write(Rfmalloc_buffer_context *ctx, R_xlen_t first_record,
                      R_xlen_t n_record, int source_type, const void *data,
                      size_t record_stride)
{
    if (!ctx || !ctx->payload || first_record < 0 || n_record < 0 ||
        (n_record && !data) || first_record > ctx->n_record ||
        n_record > ctx->n_record - first_record) {
        return -1;
    }
    if (n_record && record_stride &&
        (uint64_t)n_record > SIZE_MAX / record_stride) {
        return -1;
    }

    if (ctx->storage == RFM_BUFFER_BED &&
        source_type == RFMALLOC_BUFFER_I32) {
        size_t tight;
        if (!rfm_buffer_record_bytes(ctx->n_item, sizeof(int32_t), &tight) ||
            record_stride < tight) {
            return -1;
        }
        if (record_stride == tight) {
            return Rfmalloc_bed_write(ctx->payload, first_record, n_record,
                static_cast<const int32_t *>(data));
        }
        for (R_xlen_t i = 0; i < n_record; i++) {
            const int32_t *row = reinterpret_cast<const int32_t *>(
                static_cast<const uint8_t *>(data) + (size_t)i * record_stride);
            if (Rfmalloc_bed_write(ctx->payload, first_record + i, 1, row) != 0) {
                return -1;
            }
        }
        return 0;
    }

    if (ctx->storage == RFM_BUFFER_DOSAGE &&
        source_type == RFMALLOC_BUFFER_F64) {
        size_t tight;
        if (!rfm_buffer_record_bytes(ctx->n_item, sizeof(double), &tight) ||
            record_stride < tight) {
            return -1;
        }
        if (record_stride == tight) {
            return Rfmalloc_dosage_write(ctx->payload, first_record, n_record,
                static_cast<const double *>(data));
        }
        for (R_xlen_t i = 0; i < n_record; i++) {
            const double *row = reinterpret_cast<const double *>(
                static_cast<const uint8_t *>(data) + (size_t)i * record_stride);
            if (Rfmalloc_dosage_write(ctx->payload, first_record + i, 1, row) != 0) {
                return -1;
            }
        }
        return 0;
    }

    if (ctx->storage == RFM_BUFFER_HAPLOTYPE &&
        source_type == RFMALLOC_BUFFER_PACKED_BITS) {
        return Rfmalloc_hap_write(ctx->payload, first_record, n_record,
                                  static_cast<const uint8_t *>(data),
                                  record_stride);
    }

    if (ctx->storage == RFM_BUFFER_PLAIN_F64 &&
        source_type == RFMALLOC_BUFFER_F64) {
        size_t tight;
        if (!rfm_buffer_record_bytes(ctx->n_item, sizeof(double), &tight) ||
            record_stride < tight) {
            return -1;
        }
        fm_vector *vec = maybe_vector_from_altrep(ctx->payload);
        if (!vec || vec->type != REALSXP) {
            return -1;
        }
        double *dst = static_cast<double *>(vector_data_or_dummy(vec)) +
                      first_record * ctx->n_item;
        for (R_xlen_t i = 0; i < n_record; i++) {
            memcpy(dst + i * ctx->n_item,
                   static_cast<const uint8_t *>(data) +
                       (size_t)i * record_stride,
                   tight);
        }
        vec->maybe_dirty = true;
        return 0;
    }
    return -1;
}

extern "C" SEXP Rfmalloc_buffer_finish(Rfmalloc_buffer_context *ctx)
{
    if (!ctx || !ctx->payload) {
        return R_NilValue;
    }
    SEXP ans = PROTECT(ctx->payload);
    ctx->payload = R_NilValue;
    R_ReleaseObject(ans);
    free(ctx);
    UNPROTECT(1);
    return ans;
}

extern "C" void Rfmalloc_buffer_abort(Rfmalloc_buffer_context *ctx)
{
    if (!ctx) {
        return;
    }
    if (ctx->payload) {
        rfm_buffer_discard_payload(ctx->payload);
        R_ReleaseObject(ctx->payload);
    }
    free(ctx);
}
