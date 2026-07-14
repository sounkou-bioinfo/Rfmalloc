//==============================================================================
// PLINK 1 .bed genotypes as an fmalloc tensor codec: 2 bits per genotype.
//
// A .bed is already the storage we want. SNP-major layout stores each variant
// contiguously, and fmalloc tensors are column-major with dim[0] fastest, so a
// variant *is* a column. Four genotypes pack into a byte, low-order pair first:
//
//     00 -> 2 (homozygous, first .bim allele)   10 -> 1 (heterozygous)
//     01 -> NA (missing)                        11 -> 0 (homozygous, second)
//
// which is PLINK's dosage-of-A1 convention.
//
// Each variant is padded to a byte boundary (ceil(nrow/4) bytes), so a flat
// element index does not map affinely onto a byte offset unless nrow % 4 == 0.
// That is fine: the tensor matmul only ever decodes whole columns
// (`p0 * src.nrow`, `kb * src.nrow`), so this codec is a *self-indexing* one in
// the same sense as "alp" and "sparse" - it registers a lower-bound geometry and
// works out its own byte offsets from a header. Hence no ABI change.
//
// Payload layout (little-endian, what fmalloc stores):
//
//     [0..7)   magic "FMBED\0\0\1"
//     [8..16)  nrow  (samples)      uint64
//     [16..24) ncol  (variants)     uint64
//     [24..)   ncol * ceil(nrow/4) bytes, PLINK bit encoding
//
// i.e. a .bed body verbatim, minus its 3 magic bytes, behind a header carrying
// the one thing the .bed itself does not know (nrow lives in the .fam).
//
// Density: 2 bits/genotype. 4x tighter than bigsnpr's FBM.code256 (1 byte), 32x
// tighter than double. 500k samples x 800k variants: 100 GB, not 3.2 TB.
//==============================================================================

#define RFM_BED_HDR_BYTES 24
// magic[0..6] identify the codec; magic[7] is the kind. Existing raw payloads
// carry kind 1, so this stays backward compatible.
static const uint8_t rfm_bed_magic7[7] = {'F', 'M', 'B', 'E', 'D', 0, 0};
#define RFM_BED_RAW 1  // body is genotypes; decode returns dosages 0/1/2/NA
#define RFM_BED_STD 2  // header is followed by ncol (mu, sd) doubles, then body

// PLINK code -> dosage of the first .bim allele. Index is the 2-bit code:
// 00 -> 2 (hom A1), 01 -> missing, 10 -> 1 (het), 11 -> 0 (hom A2).
static const double rfm_bed_lut[4] = {2.0, NA_REAL, 1.0, 0.0};

struct rfm_bed_header {
    uint8_t magic[8];
    uint64_t nrow;
    uint64_t ncol;
};

static inline R_xlen_t rfm_bed_col_bytes(R_xlen_t nrow)
{
    return (nrow + 3) / 4;
}

// Byte offset of the genotype body: past the header, and past the per-variant
// (mu, sd) table when standardized.
static inline size_t rfm_bed_body_offset(int kind, R_xlen_t ncol)
{
    return (kind == RFM_BED_STD)
        ? (size_t)RFM_BED_HDR_BYTES + (size_t)ncol * 2 * sizeof(double)
        : (size_t)RFM_BED_HDR_BYTES;
}

// Decode a column-aligned element range. Returns non-zero on a malformed
// payload or a range that straddles a column, which the tensor matmul never
// produces but a direct caller could.
//
// Raw payloads decode to dosages. Standardized payloads apply, per variant, the
// 4-entry lookup table built from that variant's stored mean and sd: decode,
// mean-impute (the missing code maps to the mean, hence to 0 after centering),
// centre and scale collapse into one table read per genotype, on a pass the
// matmul was already paying for.
static int tensor_decode_bed(const void *payload, R_xlen_t elem_offset,
                             R_xlen_t n_elems, double *out)
{
    if (!payload || n_elems < 0) {
        return -1;
    }
    rfm_bed_header hdr;
    memcpy(&hdr, payload, sizeof(hdr));
    if (memcmp(hdr.magic, rfm_bed_magic7, sizeof(rfm_bed_magic7)) != 0) {
        return -1;
    }
    const int kind = hdr.magic[7];
    if (kind != RFM_BED_RAW && kind != RFM_BED_STD) {
        return -1;
    }
    const R_xlen_t nrow = (R_xlen_t)hdr.nrow;
    const R_xlen_t ncol = (R_xlen_t)hdr.ncol;
    if (nrow <= 0) {
        return -1;
    }
    if (elem_offset % nrow != 0 || n_elems % nrow != 0) {
        return -1; // whole columns only
    }

    const R_xlen_t cb = rfm_bed_col_bytes(nrow);
    const uint8_t *pbase = static_cast<const uint8_t *>(payload);
    const uint8_t *base = pbase + rfm_bed_body_offset(kind, ncol);
    const double *stats = (kind == RFM_BED_STD)
        ? reinterpret_cast<const double *>(pbase + RFM_BED_HDR_BYTES)
        : nullptr;
    const R_xlen_t c0 = elem_offset / nrow;
    const R_xlen_t nc = n_elems / nrow;

    for (R_xlen_t c = 0; c < nc; c++) {
        const R_xlen_t cj = c0 + c;
        const uint8_t *col = base + cj * cb;
        double *dst = out + c * nrow;

        // Per-column lookup table. Raw: the shared dosage table. Standardized:
        // (dosage - mu) / sd, with the missing code sent to 0 (mean imputation
        // of a mean-centred column).
        double lut[4];
        if (stats) {
            const double mu = stats[2 * cj];
            const double sd = stats[2 * cj + 1];
            lut[0] = (2.0 - mu) / sd;
            lut[1] = 0.0;
            lut[2] = (1.0 - mu) / sd;
            lut[3] = (0.0 - mu) / sd;
        } else {
            lut[0] = rfm_bed_lut[0];
            lut[1] = rfm_bed_lut[1];
            lut[2] = rfm_bed_lut[2];
            lut[3] = rfm_bed_lut[3];
        }

        R_xlen_t i = 0;
        // Whole bytes: four genotypes each, low pair first.
        for (; i + 4 <= nrow; i += 4) {
            const uint8_t b = col[i >> 2];
            dst[i]     = lut[b & 3];
            dst[i + 1] = lut[(b >> 2) & 3];
            dst[i + 2] = lut[(b >> 4) & 3];
            dst[i + 3] = lut[(b >> 6) & 3];
        }
        // Tail of a variant whose sample count is not a multiple of four; the
        // remaining high-order pairs of the last byte are padding.
        for (; i < nrow; i++) {
            dst[i] = lut[(col[i >> 2] >> (2 * (i & 3))) & 3];
        }
    }
    return 0;
}

static void tensor_register_bed_codec(void)
{
    // items_per_block = 4, bytes_per_block = 1 makes the framework's length
    // check a lower bound (ceil(n/4) bytes), which the header and per-column
    // padding only exceed. fixed_geometry is therefore false, so panels are not
    // evicted; a .bed at 2 bits/genotype is 32x smaller than the doubles it
    // decodes to, and it is the decoded panel that dominates residency.
    Rfmalloc_register_tensor_codec("bed", 4, 1, tensor_decode_bed);
}

// Allocate an empty raw bed payload. Layout construction belongs here, not in
// an importer: callers only supply shape and column panels.
extern "C" SEXP Rfmalloc_bed_create(SEXP runtime_xptr, R_xlen_t nrow,
                                     R_xlen_t ncol)
{
    fm_runtime *runtime = runtime_from_xptr(runtime_xptr);
    if (!runtime || !runtime->info) {
        Rf_error("fmalloc runtime is closed");
    }
    if (nrow <= 0 || ncol <= 0) {
        Rf_error("bed dimensions must be positive");
    }

    const R_xlen_t cb = rfm_bed_col_bytes(nrow);
    if (ncol > (std::numeric_limits<R_xlen_t>::max() - RFM_BED_HDR_BYTES) / cb) {
        Rf_error("bed payload is too large");
    }
    const R_xlen_t body = ncol * cb;
    fm_vector *vec = allocate_fm_vector(runtime, RAWSXP, RFM_BED_HDR_BYTES + body,
                                        true, false);
    SEXP ans = PROTECT(fmalloc_new_altrep(vec));
    uint8_t *p = static_cast<uint8_t *>(vector_data_or_dummy(vec));

    rfm_bed_header hdr;
    memcpy(hdr.magic, rfm_bed_magic7, sizeof(rfm_bed_magic7));
    hdr.magic[7] = RFM_BED_RAW;
    hdr.nrow = (uint64_t)nrow;
    hdr.ncol = (uint64_t)ncol;
    memcpy(p, &hdr, sizeof(hdr));

    uint8_t *base = p + RFM_BED_HDR_BYTES;
    memset(base, 0, (size_t)body);
    vec->maybe_dirty = true;
    UNPROTECT(1);
    return ans;
}

// Pack a column-major int32 panel into an existing bed payload. Returns an
// error code instead of longjmping so a stateful source reader can close cleanly.
extern "C" int Rfmalloc_bed_write(SEXP payload, R_xlen_t first_col,
                                   R_xlen_t ncol, const int32_t *src)
{
    if (first_col < 0 || ncol < 0 || (ncol && !src)) {
        return -1;
    }
    fm_vector *vec = maybe_vector_from_altrep(payload);
    if (!vec || vec->type != RAWSXP || !vec->runtime || !vec->runtime->info ||
        (size_t)vec->len < RFM_BED_HDR_BYTES) {
        return -1;
    }
    uint8_t *p = static_cast<uint8_t *>(vector_data_or_dummy(vec));
    rfm_bed_header hdr;
    memcpy(&hdr, p, sizeof(hdr));
    if (memcmp(hdr.magic, rfm_bed_magic7, sizeof(rfm_bed_magic7)) != 0 ||
        hdr.magic[7] != RFM_BED_RAW || hdr.nrow == 0 || hdr.ncol == 0 ||
        (uint64_t)first_col > hdr.ncol || (uint64_t)ncol > hdr.ncol - first_col) {
        return -1;
    }
    const R_xlen_t nrow = (R_xlen_t)hdr.nrow;
    const R_xlen_t cb = rfm_bed_col_bytes(nrow);
    if (hdr.ncol > (uint64_t)(std::numeric_limits<size_t>::max() -
                              RFM_BED_HDR_BYTES) / (uint64_t)cb ||
        (size_t)vec->len < RFM_BED_HDR_BYTES + (size_t)hdr.ncol * (size_t)cb) {
        return -1;
    }
    uint8_t *base = p + RFM_BED_HDR_BYTES;
    for (R_xlen_t c = 0; c < ncol; c++) {
        uint8_t *col = base + (first_col + c) * cb;
        memset(col, 0, (size_t)cb);
        for (R_xlen_t i = 0; i < nrow; i++) {
            const int g = src[i + c * nrow];
            uint8_t code;
            if (g == NA_INTEGER) {
                code = 1;               // missing
            } else if (g == 2) {
                code = 0;
            } else if (g == 1) {
                code = 2;
            } else if (g == 0) {
                code = 3;
            } else {
                return -1;
            }
            col[i >> 2] |= (uint8_t)(code << (2 * (i & 3)));
        }
    }
    vec->maybe_dirty = true;
    return 0;
}

//==============================================================================
// Encode an integer dosage matrix (0/1/2/NA) into a "bed" payload, so tests and
// callers can build genotype tensors without a real .bed on disk.
//==============================================================================
extern "C" SEXP rfm_tensor_bed_encode_impl(SEXP x, SEXP runtime_xptr)
{
    if (TYPEOF(x) != INTSXP) {
        Rf_error("x must be an integer matrix of dosages (0, 1, 2, NA)");
    }
    SEXP xdim = Rf_getAttrib(x, R_DimSymbol);
    if (xdim == R_NilValue || TYPEOF(xdim) != INTSXP || XLENGTH(xdim) != 2) {
        Rf_error("x must be a matrix");
    }
    const R_xlen_t nrow = (R_xlen_t)INTEGER(xdim)[0];
    const R_xlen_t ncol = (R_xlen_t)INTEGER(xdim)[1];
    SEXP ans = PROTECT(Rfmalloc_bed_create(runtime_xptr, nrow, ncol));
    if (Rfmalloc_bed_write(ans, 0, ncol, INTEGER(x)) != 0) {
        UNPROTECT(1);
        Rf_error("x contains a dosage other than 0, 1, 2 or NA");
    }
    UNPROTECT(1);
    return ans;
}

//==============================================================================
// Bake per-variant standardization into a bed tensor: one streaming pass over
// the genotypes computes each variant's mean and sd, and stores them so that
// decode returns standardized, mean-imputed values. The point is that this is
// the *only* pass: the reductions ride the same sweep the matmul's first panel
// decode would take, and thereafter every product is standardized for free.
//
//   scale_mode 0 ("sd"):       sd is the sample sd of the mean-imputed column,
//                              matching scale(impute_to_mean(as.matrix(g))).
//   scale_mode 1 ("binomial"): sd = sqrt(2 p (1-p)), p = mu/2, the allele-
//                              frequency scaling of GRM/SmartPCA/GCTA.
//
// A monomorphic variant (sd == 0) gets sd = 1, so its standardized column is all
// zeros rather than NaN.
//==============================================================================
extern "C" SEXP rfm_tensor_bed_standardize_impl(SEXP payload, SEXP runtime_xptr,
                                                SEXP scale_mode_sexp)
{
    fm_vector *in_vec = maybe_vector_from_altrep(payload);
    if (!in_vec || in_vec->type != RAWSXP) {
        Rf_error("x must be a 'bed' fmalloc tensor");
    }
    fm_runtime *runtime = runtime_from_xptr(runtime_xptr);
    if (!runtime || !runtime->info) {
        Rf_error("fmalloc runtime is closed");
    }
    const int scale_mode = Rf_asInteger(scale_mode_sexp);

    const uint8_t *ip = static_cast<const uint8_t *>(vector_data_or_dummy(in_vec));
    if ((size_t)in_vec->len < RFM_BED_HDR_BYTES) {
        Rf_error("payload too short to be a 'bed' tensor");
    }
    rfm_bed_header hdr;
    memcpy(&hdr, ip, sizeof(hdr));
    if (memcmp(hdr.magic, rfm_bed_magic7, sizeof(rfm_bed_magic7)) != 0) {
        Rf_error("payload is not a 'bed' tensor");
    }
    if (hdr.magic[7] != RFM_BED_RAW) {
        Rf_error("bed tensor is already standardized");
    }
    const R_xlen_t nrow = (R_xlen_t)hdr.nrow;
    const R_xlen_t ncol = (R_xlen_t)hdr.ncol;
    if (nrow <= 0 || ncol <= 0) {
        Rf_error("bed tensor has no data");
    }
    const R_xlen_t cb = rfm_bed_col_bytes(nrow);
    const size_t body = (size_t)ncol * (size_t)cb;
    const size_t stats_bytes = (size_t)ncol * 2 * sizeof(double);
    if ((size_t)in_vec->len < RFM_BED_HDR_BYTES + body) {
        Rf_error("bed payload too short for its declared dimensions");
    }

    fm_vector *out_vec = allocate_fm_vector(
        runtime, RAWSXP, (R_xlen_t)(RFM_BED_HDR_BYTES + stats_bytes + body), true, false);
    SEXP ans = PROTECT(fmalloc_new_altrep(out_vec));
    uint8_t *op = static_cast<uint8_t *>(vector_data_or_dummy(out_vec));

    rfm_bed_header ohdr = hdr;
    ohdr.magic[7] = RFM_BED_STD;
    memcpy(op, &ohdr, sizeof(ohdr));

    double *stats = reinterpret_cast<double *>(op + RFM_BED_HDR_BYTES);
    const uint8_t *ibody = ip + RFM_BED_HDR_BYTES;
    uint8_t *obody = op + RFM_BED_HDR_BYTES + stats_bytes;
    memcpy(obody, ibody, body); // genotypes travel with the tensor, verbatim

    for (R_xlen_t c = 0; c < ncol; c++) {
        const uint8_t *col = ibody + c * cb;
        // Sum and sum of squares over the observed (non-missing) genotypes, in
        // long double as R's own reductions do, so "sd" mode matches scale().
        long double sum = 0.0L, sumsq = 0.0L;
        R_xlen_t n_obs = 0;
        for (R_xlen_t i = 0; i < nrow; i++) {
            const unsigned code = (col[i >> 2] >> (2 * (i & 3))) & 3u;
            if (code == 1u) {
                continue; // missing
            }
            const double g = rfm_bed_lut[code];
            sum += (long double)g;
            sumsq += (long double)g * (long double)g;
            n_obs++;
        }

        double mu, sd;
        if (n_obs == 0) {
            mu = 0.0;
            sd = 1.0; // all-missing variant standardizes to 0
        } else {
            const long double mean = sum / (long double)n_obs;
            mu = (double)mean;
            if (scale_mode == 1) {
                const long double p = mean / 2.0L;
                const long double v = 2.0L * p * (1.0L - p);
                sd = v > 0.0L ? (double)sqrtl(v) : 1.0;
            } else {
                // Sample sd of the mean-imputed column of length nrow. Imputed
                // entries sit at the mean, contributing zero deviation, so the
                // sum of squared deviations is that of the observed entries:
                // sumsq - n_obs * mean^2. Denominator nrow - 1 (scale()).
                long double ss = sumsq - (long double)n_obs * mean * mean;
                if (ss < 0.0L) {
                    ss = 0.0L;
                }
                const long double denom = (nrow > 1) ? (long double)(nrow - 1) : 1.0L;
                const long double var = ss / denom;
                sd = var > 0.0L ? (double)sqrtl(var) : 1.0;
            }
            if (!(sd > 0.0)) {
                sd = 1.0;
            }
        }
        stats[2 * c] = mu;
        stats[2 * c + 1] = sd;
        R_CheckUserInterrupt();
    }

    out_vec->maybe_dirty = true;
    UNPROTECT(1);
    return ans;
}
