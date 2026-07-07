//==============================================================================
// ALP-compressed tensor codec ("alp").
//
// Lossless compression for double payloads following Afroozeh et al.,
// "ALP: Adaptive Lossless floating-Point Compression" (10.1145/3626717).
// The scalar encode/decode core is adapted from Mike Cheng's MIT-licensed C
// implementation in the zap package (github.com/coolbutuseless/zap,
// src/utils-alp.c); see inst/COPYRIGHTS.
//
// Payload container (little-endian, 8-byte aligned):
//   [u64 n_elems][u64 n_chunks][u64 chunk_byte_offsets[n_chunks]] [chunks...]
// Each chunk covers ALP_CHUNK_ELEMS doubles (the final chunk may be short):
//   header: [u8 kind][u8 e][u8 f][u8 w][u32 npatch][i64 frame_min]
//   kind 0 (packed): [u64 words[ceil(n*w/64)]]
//                    [u32 patch_idx[npatch], padded to 8 bytes]
//                    [f64 patch_val[npatch]]
//   kind 1 (raw):    [f64 values[n]]
// Chunks whose packed form would not beat raw doubles are stored raw, so the
// container never exceeds the input by more than the headers.
//==============================================================================

#define ALP_CHUNK_ELEMS 1024
#define ALP_MAX_EXP 18

struct alp_chunk_header {
    uint8_t kind;
    uint8_t e;
    uint8_t f;
    uint8_t w;
    uint32_t npatch;
    int64_t frame_min;
};

static const double alp_fact[ALP_MAX_EXP + 1] = {
    1.0, 1e1, 1e2, 1e3, 1e4, 1e5, 1e6, 1e7, 1e8, 1e9,
    1e10, 1e11, 1e12, 1e13, 1e14, 1e15, 1e16, 1e17, 1e18
};
static const double alp_invfact[ALP_MAX_EXP + 1] = {
    1.0, 1e-1, 1e-2, 1e-3, 1e-4, 1e-5, 1e-6, 1e-7, 1e-8, 1e-9,
    1e-10, 1e-11, 1e-12, 1e-13, 1e-14, 1e-15, 1e-16, 1e-17, 1e-18
};

static inline double alp_fast_round(double x)
{
    static const double sweet = (double)(1ULL << 51) + (double)(1ULL << 52);
    return x + sweet - sweet;
}

static inline bool alp_encodable(double x)
{
    return R_FINITE(x) && std::fabs(x) < 9.2233720368547748e18;
}

// Exact round-trip test for one value under scaling exponents (e, f).
// Decoding divides by the exactly representable 10^(e-f): correctly rounded
// division reproduces the nearest-double-to-decimal that round()/strtod
// produce, so decimal-rounded data patches far less than with the
// multiply-by-inverse decode (which mismatches in the last ulp).
static inline bool alp_try_encode(double x, int e, int f, int64_t *enc)
{
    if (!alp_encodable(x)) {
        return false;
    }
    int64_t candidate = (int64_t)alp_fast_round(x * alp_fact[e] * alp_invfact[f]);
    if ((double)candidate / alp_fact[e - f] != x) {
        return false;
    }
    *enc = candidate;
    return true;
}

// Pick (e, f) for one chunk by sampling: maximize exactly encodable values,
// preferring smaller e on ties (smaller encoded magnitudes -> narrower packs).
static void alp_probe_chunk(const double *x, R_xlen_t n, int *best_e, int *best_f)
{
    R_xlen_t stride = n > 32 ? n / 32 : 1;
    int top_score = -1;
    *best_e = 0;
    *best_f = 0;
    for (int e = 0; e <= ALP_MAX_EXP; e++) {
        for (int f = 0; f <= e; f++) {
            int score = 0;
            for (R_xlen_t i = 0; i < n; i += stride) {
                int64_t enc;
                if (alp_try_encode(x[i], e, f, &enc)) {
                    score++;
                }
            }
            if (score > top_score) {
                top_score = score;
                *best_e = e;
                *best_f = f;
            }
        }
    }
}

static inline size_t alp_pack_words(R_xlen_t n, unsigned w)
{
    return ((size_t)n * w + 63) / 64;
}

static void alp_pack_bits(const int64_t *in, uint64_t *words, R_xlen_t n,
                          unsigned w, int64_t frame_min)
{
    memset(words, 0, (alp_pack_words(n, w) + 1) * sizeof(uint64_t));
    size_t bitpos = 0;
    for (R_xlen_t i = 0; i < n; i++) {
        uint64_t v = (uint64_t)(in[i] - frame_min);
        size_t word = bitpos >> 6, off = bitpos & 63;
        words[word] |= v << off;
        if (off + w > 64) {
            words[word + 1] = v >> (64 - off);
        }
        bitpos += w;
    }
}

static void alp_unpack_decode(const uint64_t *words, double *out, R_xlen_t n,
                              const alp_chunk_header *hdr)
{
    uint64_t mask = (hdr->w >= 64) ? ~0ULL : ((1ULL << hdr->w) - 1);
    double divisor = alp_fact[hdr->e - hdr->f];
    size_t bitpos = 0;
    for (R_xlen_t i = 0; i < n; i++) {
        size_t word = bitpos >> 6, off = bitpos & 63;
        uint64_t v = words[word] >> off;
        if (off + (size_t)hdr->w > 64) {
            v |= words[word + 1] << (64 - off);
        }
        out[i] = (double)(hdr->frame_min + (int64_t)(v & mask)) / divisor;
        bitpos += hdr->w;
    }
}

//==============================================================================
// Encoder
//==============================================================================

static void alp_append(std::vector<uint8_t> *buf, const void *src, size_t n)
{
    const uint8_t *p = static_cast<const uint8_t *>(src);
    buf->insert(buf->end(), p, p + n);
}

static void alp_encode_chunk(const double *x, R_xlen_t n, std::vector<uint8_t> *buf)
{
    alp_chunk_header hdr;
    memset(&hdr, 0, sizeof(hdr));

    int e, f;
    alp_probe_chunk(x, n, &e, &f);

    std::vector<int64_t> enc(n);
    std::vector<uint32_t> patch_idx;
    std::vector<double> patch_val;
    for (R_xlen_t i = 0; i < n; i++) {
        if (!alp_try_encode(x[i], e, f, &enc[i])) {
            patch_idx.push_back((uint32_t)i);
            patch_val.push_back(x[i]);
            enc[i] = (i == 0) ? 0 : enc[i - 1];
        }
    }

    int64_t lo = enc[0], hi = enc[0];
    for (R_xlen_t i = 1; i < n; i++) {
        lo = enc[i] < lo ? enc[i] : lo;
        hi = enc[i] > hi ? enc[i] : hi;
    }
    unsigned w = 1;
    while (w < 64 && ((uint64_t)(hi - lo) >> w) != 0) {
        w++;
    }

    size_t idx_bytes = (patch_idx.size() * 4 + 7) & ~(size_t)7;
    size_t packed_bytes = alp_pack_words(n, w) * 8 + idx_bytes + patch_val.size() * 8;
    size_t raw_bytes = (size_t)n * 8;

    hdr.e = (uint8_t)e;
    hdr.f = (uint8_t)f;
    if (packed_bytes >= raw_bytes || w >= 64) {
        hdr.kind = 1;
        alp_append(buf, &hdr, sizeof(hdr));
        alp_append(buf, x, raw_bytes);
        return;
    }

    hdr.kind = 0;
    hdr.w = (uint8_t)w;
    hdr.npatch = (uint32_t)patch_idx.size();
    hdr.frame_min = lo;
    alp_append(buf, &hdr, sizeof(hdr));

    std::vector<uint64_t> words(alp_pack_words(n, w) + 1);
    alp_pack_bits(enc.data(), words.data(), n, w, lo);
    alp_append(buf, words.data(), alp_pack_words(n, w) * 8);

    if (!patch_idx.empty()) {
        patch_idx.resize(idx_bytes / 4, 0);
        alp_append(buf, patch_idx.data(), idx_bytes);
        alp_append(buf, patch_val.data(), patch_val.size() * 8);
    }
}

// Encode a double vector into an ALP container held in an ordinary buffer;
// the R entry point copies it into fmalloc storage afterwards.
static void alp_encode_payload(const double *x, R_xlen_t n, std::vector<uint8_t> *buf)
{
    uint64_t n_elems = (uint64_t)n;
    uint64_t n_chunks = n == 0 ? 0 : (uint64_t)((n + ALP_CHUNK_ELEMS - 1) / ALP_CHUNK_ELEMS);

    alp_append(buf, &n_elems, 8);
    alp_append(buf, &n_chunks, 8);
    size_t offsets_at = buf->size();
    buf->resize(buf->size() + n_chunks * 8);

    for (uint64_t c = 0; c < n_chunks; c++) {
        uint64_t off = (uint64_t)buf->size();
        memcpy(buf->data() + offsets_at + c * 8, &off, 8);
        R_xlen_t start = (R_xlen_t)c * ALP_CHUNK_ELEMS;
        R_xlen_t len = n - start < ALP_CHUNK_ELEMS ? n - start : ALP_CHUNK_ELEMS;
        alp_encode_chunk(x + start, len, buf);
        if ((c & 0xFF) == 0) {
            R_CheckUserInterrupt();
        }
    }
}

//==============================================================================
// Codec decode entry point
//==============================================================================

static int tensor_decode_alp(const void *payload, R_xlen_t elem_off,
                             R_xlen_t n, double *out)
{
    const uint8_t *base = static_cast<const uint8_t *>(payload);
    uint64_t n_elems, n_chunks;
    memcpy(&n_elems, base, 8);
    memcpy(&n_chunks, base + 8, 8);

    if (elem_off % ALP_CHUNK_ELEMS != 0 ||
        (uint64_t)(elem_off + n) > n_elems) {
        return -1;
    }

    const uint8_t *offsets = base + 16;
    uint64_t c = (uint64_t)(elem_off / ALP_CHUNK_ELEMS);
    R_xlen_t produced = 0;
    while (produced < n) {
        if (c >= n_chunks) {
            return -1;
        }
        uint64_t chunk_off;
        memcpy(&chunk_off, offsets + c * 8, 8);
        const uint8_t *p = base + chunk_off;

        alp_chunk_header hdr;
        memcpy(&hdr, p, sizeof(hdr));
        p += sizeof(hdr);

        R_xlen_t chunk_start = (R_xlen_t)c * ALP_CHUNK_ELEMS;
        R_xlen_t chunk_len = (R_xlen_t)n_elems - chunk_start < ALP_CHUNK_ELEMS
                                 ? (R_xlen_t)n_elems - chunk_start
                                 : ALP_CHUNK_ELEMS;
        R_xlen_t want = n - produced < chunk_len ? n - produced : chunk_len;

        if (hdr.kind == 1) {
            memcpy(out + produced, p, (size_t)want * 8);
        } else {
            // Full-chunk decode into a stack buffer, then copy the prefix the
            // caller asked for (the tail chunk may be requested short).
            double tmp[ALP_CHUNK_ELEMS];
            alp_unpack_decode(reinterpret_cast<const uint64_t *>(p), tmp,
                              chunk_len, &hdr);
            const uint8_t *patches = p + alp_pack_words(chunk_len, hdr.w) * 8;
            size_t idx_bytes = ((size_t)hdr.npatch * 4 + 7) & ~(size_t)7;
            const double *vals = reinterpret_cast<const double *>(patches + idx_bytes);
            for (uint32_t i = 0; i < hdr.npatch; i++) {
                uint32_t idx;
                memcpy(&idx, patches + i * 4, 4);
                if (idx >= (uint32_t)chunk_len) {
                    return -1;
                }
                memcpy(&tmp[idx], &vals[i], 8);
            }
            memcpy(out + produced, tmp, (size_t)want * 8);
        }
        produced += want;
        c++;
    }
    return 0;
}

static void tensor_register_alp_codec(void)
{
    // bytes_per_block is 1: the container is variable-size and self-indexing,
    // so the fixed-geometry length validation degenerates to a lower bound.
    Rfmalloc_register_tensor_codec("alp", ALP_CHUNK_ELEMS, 1, tensor_decode_alp);
}

//==============================================================================
// R entry point: encode a double vector/matrix into an fmalloc ALP tensor
// payload allocated in the given runtime. Returns list(payload, nonfinite).
//==============================================================================

extern "C" SEXP rfm_tensor_alp_encode_impl(SEXP x, SEXP runtime_xptr)
{
    if (TYPEOF(x) != REALSXP) {
        Rf_error("ALP encoding requires a double vector or matrix");
    }
    fm_runtime *runtime = runtime_from_xptr(runtime_xptr);

    R_xlen_t n = XLENGTH(x);
    const double *values = REAL(x);
    bool nonfinite = false;
    for (R_xlen_t i = 0; i < n; i++) {
        if (!R_FINITE(values[i])) {
            nonfinite = true;
            break;
        }
    }

    std::vector<uint8_t> buf;
    alp_encode_payload(values, n, &buf);

    fm_vector *vec = allocate_fm_vector(runtime, RAWSXP, (R_xlen_t)buf.size(),
                                        true, false);
    SEXP payload = PROTECT(fmalloc_new_altrep(vec));
    memcpy(vector_data_or_dummy(vec), buf.data(), buf.size());

    SEXP ans = PROTECT(Rf_allocVector(VECSXP, 2));
    SET_VECTOR_ELT(ans, 0, payload);
    SET_VECTOR_ELT(ans, 1, Rf_ScalarLogical(nonfinite));
    UNPROTECT(2);
    return ans;
}
