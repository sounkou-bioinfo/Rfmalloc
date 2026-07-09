//==============================================================================
// Phased haplotypes as a plain fmalloc raw payload: 1 bit per allele call.
//
// This is a SIBLING interface to the matmul tensor codec ABI above, not a
// codec. Haplotype HMM methods (Li & Stephens local-ancestry inference, as
// implemented by the "kalis" package) are not linear algebra: their compute
// is forward-backward recursion over a hidden Markov chain, never a GEMM. So
// this store never calls Rfmalloc_register_tensor_codec and never
// participates in "%*%" / fmalloc_tensor_materialize() - it is a second,
// independent typed accessor over the same fmalloc storage substrate, with
// its own encode/decode pair, reachable only through fmalloc_haplotypes()
// and fmalloc_hap_materialize().
//
// Layout mirrors "bed"/"dosage"'s header convention (24-byte header, a magic
// + kind, then the body), because that convention already answers the right
// questions (shape, kind), but the packing itself is the simplest possible
// one: 1 bit per call, no lookup table, no missing-value code point (kalis's
// own CacheHaplotypes() rejects anything other than 0/1, so there is no
// missing-value case to represent).
//
// kalis wants an L (variants) x N (haplotypes) matrix, haplotypes in columns
// - a haplotype's calls across all variants are contiguous down a column,
// which is exactly R's own column-major storage order. So encoding packs
// each column into ceil(L/8) bytes, low-order bit first (variant 0 in bit 0
// of the first byte), the same "column is the packing unit" structure "bed"
// uses for its own column-major PLINK convention.
//
//     [0..7)   magic 'F','M','H','A','P',0,0, kind   (kind 1: only kind so far)
//     [8..16)  L (variants, rows)   uint64
//     [16..24) N (haplotypes, cols) uint64
//     [24..)   N * ceil(L/8) bytes, 1 bit/call, column-major, LSB-first
//
// Density: 1 bit/call. 8x tighter than an integer 0/1 matrix (4 bytes/call),
// 64x tighter than the double matrix a naive R pipeline would build.
//==============================================================================

#define RFM_HAP_HDR_BYTES 24
static const uint8_t rfm_hap_magic7[7] = {'F', 'M', 'H', 'A', 'P', 0, 0};
#define RFM_HAP_RAW 1

struct rfm_hap_header {
    uint8_t magic[8];
    uint64_t L;  // variants (rows)
    uint64_t N;  // haplotypes (columns)
};

static inline R_xlen_t rfm_hap_col_bytes(R_xlen_t L)
{
    return (L + 7) / 8;
}

//==============================================================================
// Encode an L x N integer matrix of 0/1 haplotype calls (variants in rows,
// haplotypes in columns - kalis's own convention) into a bit-packed fmalloc
// raw payload.
//==============================================================================
extern "C" SEXP rfm_hap_encode_impl(SEXP x, SEXP runtime_xptr)
{
    fm_runtime *runtime = runtime_from_xptr(runtime_xptr);
    if (!runtime || !runtime->info) {
        Rf_error("fmalloc runtime is closed");
    }
    if (TYPEOF(x) != INTSXP) {
        Rf_error("x must be an integer matrix of haplotype calls (0 or 1)");
    }
    SEXP xdim = Rf_getAttrib(x, R_DimSymbol);
    if (xdim == R_NilValue || TYPEOF(xdim) != INTSXP || XLENGTH(xdim) != 2) {
        Rf_error("x must be a matrix");
    }
    const R_xlen_t L = (R_xlen_t)INTEGER(xdim)[0];  // variants
    const R_xlen_t N = (R_xlen_t)INTEGER(xdim)[1];  // haplotypes
    if (L <= 0 || N <= 0) {
        Rf_error("x must have at least one row (variant) and one column (haplotype)");
    }

    const R_xlen_t cb = rfm_hap_col_bytes(L);
    const R_xlen_t body = N * cb;
    fm_vector *vec = allocate_fm_vector(runtime, RAWSXP, RFM_HAP_HDR_BYTES + body,
                                        true, false);
    SEXP ans = PROTECT(fmalloc_new_altrep(vec));
    uint8_t *p = static_cast<uint8_t *>(vector_data_or_dummy(vec));

    rfm_hap_header hdr;
    memcpy(hdr.magic, rfm_hap_magic7, sizeof(rfm_hap_magic7));
    hdr.magic[7] = RFM_HAP_RAW;
    hdr.L = (uint64_t)L;
    hdr.N = (uint64_t)N;
    memcpy(p, &hdr, sizeof(hdr));

    uint8_t *base = p + RFM_HAP_HDR_BYTES;
    memset(base, 0, (size_t)body);
    const int *src = INTEGER(x);
    for (R_xlen_t c = 0; c < N; c++) {
        uint8_t *col = base + c * cb;
        for (R_xlen_t i = 0; i < L; i++) {
            const int g = src[i + c * L];
            if (g == 1) {
                col[i >> 3] |= (uint8_t)(1u << (i & 7));
            } else if (g != 0) {
                UNPROTECT(1);
                Rf_error("haplotype calls must be 0 or 1, got %d", g);
            }
        }
        R_CheckUserInterrupt();
    }
    vec->maybe_dirty = true;
    UNPROTECT(1);
    return ans;
}

//==============================================================================
// Decode a bit-packed haplotype payload back into a fresh fmalloc-backed
// integer matrix (still file-backed storage, not an R-heap allocation) of
// dosage-of-1 calls, L x N, ready to hand to kalis::CacheHaplotypes().
//==============================================================================
extern "C" SEXP rfm_hap_materialize_impl(SEXP payload, SEXP runtime_xptr)
{
    fm_vector *in_vec = maybe_vector_from_altrep(payload);
    if (!in_vec || in_vec->type != RAWSXP) {
        Rf_error("x must be an fmalloc_haplotypes payload");
    }
    fm_runtime *runtime = runtime_from_xptr(runtime_xptr);
    if (!runtime || !runtime->info) {
        Rf_error("fmalloc runtime is closed");
    }

    const uint8_t *ip = static_cast<const uint8_t *>(vector_data_or_dummy(in_vec));
    if ((size_t)in_vec->len < RFM_HAP_HDR_BYTES) {
        Rf_error("payload too short to be an fmalloc_haplotypes tensor");
    }
    rfm_hap_header hdr;
    memcpy(&hdr, ip, sizeof(hdr));
    if (memcmp(hdr.magic, rfm_hap_magic7, sizeof(rfm_hap_magic7)) != 0) {
        Rf_error("payload is not an fmalloc_haplotypes tensor");
    }
    if (hdr.magic[7] != RFM_HAP_RAW) {
        Rf_error("unrecognized fmalloc_haplotypes payload kind");
    }
    const R_xlen_t L = (R_xlen_t)hdr.L;
    const R_xlen_t N = (R_xlen_t)hdr.N;
    if (L <= 0 || N <= 0) {
        Rf_error("fmalloc_haplotypes tensor has no data");
    }
    const R_xlen_t cb = rfm_hap_col_bytes(L);
    const size_t body = (size_t)N * (size_t)cb;
    if ((size_t)in_vec->len < RFM_HAP_HDR_BYTES + body) {
        Rf_error("fmalloc_haplotypes payload too short for its declared dimensions");
    }

    const R_xlen_t total = L * N;
    fm_vector *out_vec = allocate_fm_vector(runtime, INTSXP, total, true, false);
    SEXP ans = PROTECT(fmalloc_new_altrep(out_vec));
    int *op = static_cast<int *>(vector_data_or_dummy(out_vec));

    const uint8_t *ibody = ip + RFM_HAP_HDR_BYTES;
    for (R_xlen_t c = 0; c < N; c++) {
        const uint8_t *col = ibody + c * cb;
        int *dst = op + c * L;
        for (R_xlen_t i = 0; i < L; i++) {
            dst[i] = (col[i >> 3] >> (i & 7)) & 1;
        }
        R_CheckUserInterrupt();
    }
    out_vec->maybe_dirty = true;
    UNPROTECT(1);
    return ans;
}
