//==============================================================================
// Sparse tensor codec ("sparse") for mostly-zero data (e.g. single-cell
// counts). Values are stored per 1024-element chunk as (local index, value)
// pairs for the nonzeros only; zeros are implicit. Like the ALP codec this is
// a self-indexing container (variable per-chunk size + a chunk offset table),
// so it decodes block-aligned element ranges to dense doubles and rides the
// panel-streaming matrix products.
//
// Payload container (little-endian, 8-byte aligned):
//   [u64 n_elems][u64 n_chunks][u64 chunk_byte_offsets[n_chunks]] [chunks...]
// Each chunk (SPARSE_CHUNK_ELEMS elements; the last may be short):
//   [u32 nnz][u32 pad][u32 idx[nnz] padded to 8 bytes][f64 val[nnz]]
//==============================================================================

#define SPARSE_CHUNK_ELEMS 1024

static void sparse_append(std::vector<uint8_t> *buf, const void *src, size_t n)
{
    const uint8_t *p = static_cast<const uint8_t *>(src);
    buf->insert(buf->end(), p, p + n);
}

static void sparse_encode_chunk(const double *x, R_xlen_t n, std::vector<uint8_t> *buf,
                                bool *nonfinite)
{
    std::vector<uint32_t> idx;
    std::vector<double> val;
    for (R_xlen_t i = 0; i < n; i++) {
        if (x[i] != 0.0) {
            idx.push_back((uint32_t)i);
            val.push_back(x[i]);
            if (!R_FINITE(x[i])) {
                *nonfinite = true;
            }
        }
    }
    uint32_t nnz = (uint32_t)idx.size();
    uint32_t header[2] = {nnz, 0};
    sparse_append(buf, header, sizeof(header));

    size_t idx_bytes = ((size_t)nnz * 4 + 7) & ~(size_t)7;
    if (nnz > 0) {
        idx.resize(idx_bytes / 4, 0);
        sparse_append(buf, idx.data(), idx_bytes);
        sparse_append(buf, val.data(), (size_t)nnz * 8);
    }
}

static void sparse_encode_payload(const double *x, R_xlen_t n,
                                  std::vector<uint8_t> *buf, bool *nonfinite)
{
    uint64_t n_elems = (uint64_t)n;
    uint64_t n_chunks = n == 0 ? 0
        : (uint64_t)((n + SPARSE_CHUNK_ELEMS - 1) / SPARSE_CHUNK_ELEMS);

    sparse_append(buf, &n_elems, 8);
    sparse_append(buf, &n_chunks, 8);
    size_t offsets_at = buf->size();
    buf->resize(buf->size() + n_chunks * 8);

    for (uint64_t c = 0; c < n_chunks; c++) {
        uint64_t off = (uint64_t)buf->size();
        memcpy(buf->data() + offsets_at + c * 8, &off, 8);
        R_xlen_t start = (R_xlen_t)c * SPARSE_CHUNK_ELEMS;
        R_xlen_t len = n - start < SPARSE_CHUNK_ELEMS ? n - start : SPARSE_CHUNK_ELEMS;
        sparse_encode_chunk(x + start, len, buf, nonfinite);
        if ((c & 0xFF) == 0) {
            R_CheckUserInterrupt();
        }
    }
}

static int tensor_decode_sparse(const void *payload, R_xlen_t elem_off,
                                R_xlen_t n, double *out)
{
    const uint8_t *base = static_cast<const uint8_t *>(payload);
    uint64_t n_elems, n_chunks;
    memcpy(&n_elems, base, 8);
    memcpy(&n_chunks, base + 8, 8);

    if (elem_off % SPARSE_CHUNK_ELEMS != 0 || (uint64_t)(elem_off + n) > n_elems) {
        return -1;
    }

    const uint8_t *offsets = base + 16;
    uint64_t c = (uint64_t)(elem_off / SPARSE_CHUNK_ELEMS);
    R_xlen_t produced = 0;
    while (produced < n) {
        if (c >= n_chunks) {
            return -1;
        }
        uint64_t chunk_off;
        memcpy(&chunk_off, offsets + c * 8, 8);
        const uint8_t *p = base + chunk_off;

        uint32_t nnz;
        memcpy(&nnz, p, 4);

        R_xlen_t chunk_start = (R_xlen_t)c * SPARSE_CHUNK_ELEMS;
        R_xlen_t chunk_len = (R_xlen_t)n_elems - chunk_start < SPARSE_CHUNK_ELEMS
                                 ? (R_xlen_t)n_elems - chunk_start
                                 : SPARSE_CHUNK_ELEMS;
        R_xlen_t want = n - produced < chunk_len ? n - produced : chunk_len;

        double *o = out + produced;
        memset(o, 0, (size_t)want * sizeof(double));

        if (nnz > 0) {
            const uint8_t *idx_p = p + 8; // after [u32 nnz][u32 pad]
            size_t idx_bytes = ((size_t)nnz * 4 + 7) & ~(size_t)7;
            const double *vals = reinterpret_cast<const double *>(idx_p + idx_bytes);
            for (uint32_t k = 0; k < nnz; k++) {
                uint32_t li;
                memcpy(&li, idx_p + k * 4, 4);
                if ((R_xlen_t)li < want) {
                    memcpy(&o[li], &vals[k], 8);
                }
            }
        }
        produced += want;
        c++;
    }
    return 0;
}

static void tensor_register_sparse_codec(void)
{
    Rfmalloc_register_tensor_codec("sparse", SPARSE_CHUNK_ELEMS, 1, tensor_decode_sparse);
}

// Encode a double vector/matrix into a "sparse" fmalloc tensor payload in the
// given runtime. Returns list(payload, nonfinite).
extern "C" SEXP rfm_tensor_sparse_encode_impl(SEXP x, SEXP runtime_xptr)
{
    if (TYPEOF(x) != REALSXP) {
        Rf_error("sparse encoding requires a double vector or matrix");
    }
    fm_runtime *runtime = runtime_from_xptr(runtime_xptr);

    std::vector<uint8_t> buf;
    bool nonfinite = false;
    sparse_encode_payload(REAL(x), XLENGTH(x), &buf, &nonfinite);

    fm_vector *vec = allocate_fm_vector(runtime, RAWSXP, (R_xlen_t)buf.size(), true, false);
    SEXP payload = PROTECT(fmalloc_new_altrep(vec));
    memcpy(vector_data_or_dummy(vec), buf.data(), buf.size());

    SEXP ans = PROTECT(Rf_allocVector(VECSXP, 2));
    SET_VECTOR_ELT(ans, 0, payload);
    SET_VECTOR_ELT(ans, 1, Rf_ScalarLogical(nonfinite));
    UNPROTECT(2);
    return ans;
}
