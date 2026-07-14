//==============================================================================
// Phased haplotypes as a locus-major fmalloc bit matrix.
//
// The R view is L variants by N haplotypes. Storage is deliberately transposed
// from R's column-major matrix layout: each locus owns one contiguous bit row
// over all N haplotypes. Li and Stephens HMM kernels advance locus by locus and
// vectorize over donor haplotypes. kalis uses this exact layout internally as
// hap_locus[l][hap / 32], with each locus beginning on a SIMD boundary.
//
//     [header and alignment padding]
//     [locus 0: ceil(N / 8) bits, padded to 64 bytes]
//     [locus 1: ceil(N / 8) bits, padded to 64 bytes]
//     ...
//
// The body and every row are 64-byte aligned. The small padding cost buys the
// access pattern the compute actually consumes and lets a future HMM backend
// map locus rows directly instead of repacking a haplotype-major file.
//==============================================================================

#define RFM_HAP_PREFIX_BYTES 64
#define RFM_HAP_ALIGN 64
static const uint8_t rfm_hap_magic7[7] = {'F', 'M', 'H', 'A', 'P', 0, 0};
#define RFM_HAP_LOCUS_MAJOR 2

struct rfm_hap_header {
    uint8_t magic[8];
    uint64_t L;
    uint64_t N;
    uint64_t stride;
    uint64_t body_offset;
    uint8_t reserved[24];
};

static inline size_t rfm_hap_row_bytes(R_xlen_t N)
{
    return ((size_t)N + 7) / 8;
}

static inline size_t rfm_hap_stride(R_xlen_t N)
{
    const size_t bytes = rfm_hap_row_bytes(N);
    return (bytes + RFM_HAP_ALIGN - 1) & ~(size_t)(RFM_HAP_ALIGN - 1);
}

static int rfm_hap_view(SEXP payload, fm_vector **vec_out,
                        rfm_hap_header *hdr_out, uint8_t **body_out)
{
    fm_vector *vec = maybe_vector_from_altrep(payload);
    if (!vec || vec->type != RAWSXP || !vec->runtime || !vec->runtime->info ||
        (size_t)vec->len < RFM_HAP_PREFIX_BYTES) {
        return -1;
    }
    uint8_t *p = static_cast<uint8_t *>(vector_data_or_dummy(vec));
    rfm_hap_header hdr;
    memcpy(&hdr, p, sizeof(hdr));
    if (memcmp(hdr.magic, rfm_hap_magic7, sizeof(rfm_hap_magic7)) != 0 ||
        hdr.magic[7] != RFM_HAP_LOCUS_MAJOR || hdr.L == 0 || hdr.N == 0 ||
        hdr.L > (uint64_t)std::numeric_limits<R_xlen_t>::max() ||
        hdr.N > (uint64_t)std::numeric_limits<R_xlen_t>::max() ||
        hdr.stride != rfm_hap_stride((R_xlen_t)hdr.N) ||
        hdr.body_offset < RFM_HAP_PREFIX_BYTES ||
        hdr.body_offset >= RFM_HAP_PREFIX_BYTES + RFM_HAP_ALIGN ||
        hdr.L > (uint64_t)(std::numeric_limits<size_t>::max() -
                           hdr.body_offset) / hdr.stride ||
        (size_t)vec->len < (size_t)hdr.body_offset +
                           (size_t)hdr.L * (size_t)hdr.stride) {
        return -1;
    }
    if (vec_out) {
        *vec_out = vec;
    }
    if (hdr_out) {
        *hdr_out = hdr;
    }
    if (body_out) {
        *body_out = p + hdr.body_offset;
    }
    return 0;
}

// Allocate an empty locus-major store. L is the number of records and N is
// the number of packed calls in each record.
extern "C" SEXP Rfmalloc_hap_create(SEXP runtime_xptr, R_xlen_t L,
                                     R_xlen_t N)
{
    fm_runtime *runtime = runtime_from_xptr(runtime_xptr);
    if (!runtime || !runtime->info) {
        Rf_error("fmalloc runtime is closed");
    }
    if (L <= 0 || N <= 0) {
        Rf_error("haplotype dimensions must be positive");
    }
    const size_t stride = rfm_hap_stride(N);
    if ((uint64_t)L > (uint64_t)(std::numeric_limits<R_xlen_t>::max() -
                                 RFM_HAP_PREFIX_BYTES - RFM_HAP_ALIGN + 1) /
                          stride) {
        Rf_error("haplotype payload is too large");
    }
    const R_xlen_t len = RFM_HAP_PREFIX_BYTES + RFM_HAP_ALIGN - 1 +
                         L * (R_xlen_t)stride;
    fm_vector *vec = allocate_fm_vector(runtime, RAWSXP, len, true, false);
    SEXP ans = PROTECT(fmalloc_new_altrep(vec));
    uint8_t *p = static_cast<uint8_t *>(vector_data_or_dummy(vec));
    const uintptr_t start = (uintptr_t)p + RFM_HAP_PREFIX_BYTES;
    const size_t body_offset = (size_t)((start + RFM_HAP_ALIGN - 1) &
                                        ~(uintptr_t)(RFM_HAP_ALIGN - 1)) -
                               (uintptr_t)p;

    rfm_hap_header hdr;
    memset(&hdr, 0, sizeof(hdr));
    memcpy(hdr.magic, rfm_hap_magic7, sizeof(rfm_hap_magic7));
    hdr.magic[7] = RFM_HAP_LOCUS_MAJOR;
    hdr.L = (uint64_t)L;
    hdr.N = (uint64_t)N;
    hdr.stride = (uint64_t)stride;
    hdr.body_offset = (uint64_t)body_offset;
    memcpy(p, &hdr, sizeof(hdr));
    memset(p + body_offset, 0, (size_t)L * stride);
    vec->maybe_dirty = true;
    UNPROTECT(1);
    return ans;
}

// Copy packed, locus-major rows into an existing store. src_stride is the
// distance between input rows and may be the unpadded ceil(N / 8).
extern "C" int Rfmalloc_hap_write(SEXP payload, R_xlen_t first_locus,
                                   R_xlen_t n_locus, const uint8_t *src,
                                   size_t src_stride)
{
    if (first_locus < 0 || n_locus < 0 || (n_locus && !src)) {
        return -1;
    }
    fm_vector *vec;
    rfm_hap_header hdr;
    uint8_t *body;
    if (rfm_hap_view(payload, &vec, &hdr, &body) != 0 ||
        (uint64_t)first_locus > hdr.L ||
        (uint64_t)n_locus > hdr.L - (uint64_t)first_locus) {
        return -1;
    }
    const size_t row_bytes = rfm_hap_row_bytes((R_xlen_t)hdr.N);
    if (src_stride < row_bytes) {
        return -1;
    }
    const unsigned int tail = (unsigned int)(hdr.N & 7);
    for (R_xlen_t l = 0; l < n_locus; l++) {
        uint8_t *dst = body + (size_t)(first_locus + l) * (size_t)hdr.stride;
        memcpy(dst, src + (size_t)l * src_stride, row_bytes);
        if (tail) {
            dst[row_bytes - 1] &= (uint8_t)((1u << tail) - 1u);
        }
        memset(dst + row_bytes, 0, (size_t)hdr.stride - row_bytes);
    }
    vec->maybe_dirty = true;
    return 0;
}

// Encode an R matrix without constructing a transposed intermediate.
extern "C" SEXP rfm_hap_encode_impl(SEXP x, SEXP runtime_xptr)
{
    if (TYPEOF(x) != INTSXP) {
        Rf_error("x must be an integer matrix of haplotype calls (0 or 1)");
    }
    SEXP xdim = Rf_getAttrib(x, R_DimSymbol);
    if (xdim == R_NilValue || TYPEOF(xdim) != INTSXP || XLENGTH(xdim) != 2) {
        Rf_error("x must be a matrix");
    }
    const R_xlen_t L = (R_xlen_t)INTEGER(xdim)[0];
    const R_xlen_t N = (R_xlen_t)INTEGER(xdim)[1];
    SEXP ans = PROTECT(Rfmalloc_hap_create(runtime_xptr, L, N));
    fm_vector *vec;
    rfm_hap_header hdr;
    uint8_t *body;
    if (rfm_hap_view(ans, &vec, &hdr, &body) != 0) {
        UNPROTECT(1);
        Rf_error("failed to initialize haplotype payload");
    }

    const int *src = INTEGER(x);
    for (R_xlen_t l = 0; l < L; l++) {
        uint8_t *row = body + (size_t)l * (size_t)hdr.stride;
        for (R_xlen_t h = 0; h < N; h++) {
            const int call = src[l + h * L];
            if (call == 1) {
                row[h >> 3] |= (uint8_t)(1u << (h & 7));
            } else if (call != 0) {
                UNPROTECT(1);
                Rf_error("haplotype calls must be 0 or 1, got %d", call);
            }
        }
        if ((l & 1023) == 1023) {
            R_CheckUserInterrupt();
        }
    }
    vec->maybe_dirty = true;
    UNPROTECT(1);
    return ans;
}

// Decode back to the conventional L by N R matrix. The result remains an
// fmalloc-backed integer matrix.
extern "C" SEXP rfm_hap_materialize_impl(SEXP payload, SEXP runtime_xptr)
{
    fm_vector *in_vec;
    rfm_hap_header hdr;
    uint8_t *body;
    if (rfm_hap_view(payload, &in_vec, &hdr, &body) != 0) {
        Rf_error("x must be a valid locus-major fmalloc_haplotypes object");
    }
    fm_runtime *runtime = runtime_from_xptr(runtime_xptr);
    if (!runtime || !runtime->info) {
        Rf_error("fmalloc runtime is closed");
    }
    if (hdr.L > (uint64_t)std::numeric_limits<R_xlen_t>::max() / hdr.N) {
        Rf_error("haplotype matrix is too large");
    }
    const R_xlen_t L = (R_xlen_t)hdr.L;
    const R_xlen_t N = (R_xlen_t)hdr.N;
    fm_vector *out_vec = allocate_fm_vector(runtime, INTSXP, L * N, true, false);
    SEXP ans = PROTECT(fmalloc_new_altrep(out_vec));
    int *out = static_cast<int *>(vector_data_or_dummy(out_vec));

    for (R_xlen_t l = 0; l < L; l++) {
        const uint8_t *row = body + (size_t)l * (size_t)hdr.stride;
        for (R_xlen_t h = 0; h < N; h++) {
            out[l + h * L] = (row[h >> 3] >> (h & 7)) & 1;
        }
        if ((l & 1023) == 1023) {
            R_CheckUserInterrupt();
        }
    }
    out_vec->maybe_dirty = true;
    UNPROTECT(1);
    return ans;
}
