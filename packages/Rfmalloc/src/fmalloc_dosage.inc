//==============================================================================
// Fractional genotype dosages as an fmalloc tensor codec: 1 byte per dosage.
//
// The continuous sibling of the "bed" codec. PLINK 2 .pgen and imputation output
// carry dosages in [0, 2] rather than hardcalls, so a 2-bit code cannot hold
// them. This codec stores each dosage as a fixed-point byte:
//
//     d in [0, 2]  ->  b = lround(d * 127)  in 0..254
//     b == 255      ->  missing (NA)
//     decode:  d = b / 127
//
// Lossy by construction (resolution 2/254 ~ 0.0079); that is the point of a
// storage codec. 8x tighter than the doubles it decodes to, and it is the target
// a .pgen dosage import re-encodes into (pgen's own reader is stateful and
// LD-compressed, so we decode through it once at import and store this instead).
//
// Layout mirrors "bed" exactly: a 24-byte header carrying the shape and a kind
// byte, then the body, with an optional per-variant (mu, sd) table when
// standardized. Columns are contiguous (dim[0] fastest), so a variant is a
// column and the matmul decodes whole columns.
//
//     [0..7)   magic 'F','M','D','O','S',0,0, kind   (kind 1 raw, 2 standardized)
//     [8..16)  nrow  uint64      (samples)
//     [16..24) ncol  uint64      (variants)
//     kind 1:  [24..)              nrow*ncol dosage bytes, column-major
//     kind 2:  [24 .. 24+ncol*16)  ncol (mu, sd) double pairs
//              [24+ncol*16 ..)     the dosage bytes, verbatim
//==============================================================================

#define RFM_DOS_HDR_BYTES 24
#define RFM_DOS_MISSING 255u
#define RFM_DOS_SCALE 127.0  // b = round(d * 127), d = b / 127, so 0..254 == 0..2

static const uint8_t rfm_dos_magic7[7] = {'F', 'M', 'D', 'O', 'S', 0, 0};
#define RFM_DOS_RAW 1
#define RFM_DOS_STD 2

struct rfm_dos_header {
    uint8_t magic[8];
    uint64_t nrow;
    uint64_t ncol;
};

// Byte offset of the dosage body: past the header, and past the per-variant
// (mu, sd) table when standardized.
static inline size_t rfm_dos_body_offset(int kind, R_xlen_t ncol)
{
    return (kind == RFM_DOS_STD)
        ? (size_t)RFM_DOS_HDR_BYTES + (size_t)ncol * 2 * sizeof(double)
        : (size_t)RFM_DOS_HDR_BYTES;
}

// Shared raw lookup table, byte -> dosage with 255 -> NA. Filled once at codec
// registration (package load, single threaded), so decode never races to
// initialize it: by the time any tensor exists, the table is ready.
static double rfm_dos_raw_lut[256];

// Decode a column-aligned element range. Returns non-zero on a malformed payload
// or a range that straddles a column, which the tensor matmul never produces but
// a direct caller could.
//
// Raw payloads decode to dosages. Standardized payloads apply, per variant, the
// 256-entry table built from that variant's stored mean and sd: decode,
// mean-impute (the missing code maps to the mean, hence to 0 after centering),
// centre and scale collapse into one table read per genotype.
static int tensor_decode_dosage(const void *payload, R_xlen_t elem_offset,
                                R_xlen_t n_elems, double *out)
{
    if (!payload || n_elems < 0) {
        return -1;
    }
    rfm_dos_header hdr;
    memcpy(&hdr, payload, sizeof(hdr));
    if (memcmp(hdr.magic, rfm_dos_magic7, sizeof(rfm_dos_magic7)) != 0) {
        return -1;
    }
    const int kind = hdr.magic[7];
    if (kind != RFM_DOS_RAW && kind != RFM_DOS_STD) {
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

    const uint8_t *pbase = static_cast<const uint8_t *>(payload);
    const uint8_t *base = pbase + rfm_dos_body_offset(kind, ncol);
    const double *stats = (kind == RFM_DOS_STD)
        ? reinterpret_cast<const double *>(pbase + RFM_DOS_HDR_BYTES)
        : nullptr;
    const R_xlen_t c0 = elem_offset / nrow;
    const R_xlen_t nc = n_elems / nrow;

    for (R_xlen_t c = 0; c < nc; c++) {
        const R_xlen_t cj = c0 + c;
        const uint8_t *col = base + cj * nrow;
        double *dst = out + c * nrow;

        // Per-column lookup table. Raw: the shared byte->dosage table.
        // Standardized: (dosage - mu) / sd, with the missing code sent to 0
        // (mean imputation of a mean-centred column).
        const double *lut;
        double lut_std[256];
        if (stats) {
            const double mu = stats[2 * cj];
            const double sd = stats[2 * cj + 1];
            for (int b = 0; b < 255; b++) {
                lut_std[b] = ((double)b / RFM_DOS_SCALE - mu) / sd;
            }
            lut_std[RFM_DOS_MISSING] = 0.0;
            lut = lut_std;
        } else {
            lut = rfm_dos_raw_lut;
        }

        for (R_xlen_t i = 0; i < nrow; i++) {
            dst[i] = lut[col[i]];
        }
    }
    return 0;
}

static void tensor_register_dosage_codec(void)
{
    for (int b = 0; b < 255; b++) {
        rfm_dos_raw_lut[b] = (double)b / RFM_DOS_SCALE;
    }
    rfm_dos_raw_lut[RFM_DOS_MISSING] = NA_REAL;

    // items_per_block = 1, bytes_per_block = 1 makes the framework's length check
    // a lower bound, which the header and (when standardized) the stats table
    // only exceed. fixed_geometry is therefore false, as for "bed": panels are
    // not evicted, and the decoded panel (8 bytes/dosage) dominates residency
    // over the 1-byte source anyway.
    Rfmalloc_register_tensor_codec("dosage", 1, 1, tensor_decode_dosage);
}

extern "C" SEXP Rfmalloc_dosage_create(SEXP runtime_xptr, R_xlen_t nrow,
                                        R_xlen_t ncol)
{
    fm_runtime *runtime = runtime_from_xptr(runtime_xptr);
    if (!runtime || !runtime->info) {
        Rf_error("fmalloc runtime is closed");
    }
    if (nrow <= 0 || ncol <= 0) {
        Rf_error("dosage dimensions must be positive");
    }
    if (nrow > (std::numeric_limits<R_xlen_t>::max() - RFM_DOS_HDR_BYTES) /
                   ncol) {
        Rf_error("dosage payload is too large");
    }

    const R_xlen_t body = nrow * ncol;
    fm_vector *vec = allocate_fm_vector(runtime, RAWSXP, RFM_DOS_HDR_BYTES + body,
                                        true, false);
    SEXP ans = PROTECT(fmalloc_new_altrep(vec));
    uint8_t *p = static_cast<uint8_t *>(vector_data_or_dummy(vec));

    rfm_dos_header hdr;
    memcpy(hdr.magic, rfm_dos_magic7, sizeof(rfm_dos_magic7));
    hdr.magic[7] = RFM_DOS_RAW;
    hdr.nrow = (uint64_t)nrow;
    hdr.ncol = (uint64_t)ncol;
    memcpy(p, &hdr, sizeof(hdr));

    uint8_t *base = p + RFM_DOS_HDR_BYTES;
    memset(base, RFM_DOS_MISSING, (size_t)body);
    vec->maybe_dirty = true;
    UNPROTECT(1);
    return ans;
}

extern "C" int Rfmalloc_dosage_write(SEXP payload, R_xlen_t first_col,
                                      R_xlen_t ncol, const double *src)
{
    if (first_col < 0 || ncol < 0 || (ncol && !src)) {
        return -1;
    }
    fm_vector *vec = maybe_vector_from_altrep(payload);
    if (!vec || vec->type != RAWSXP || !vec->runtime || !vec->runtime->info ||
        (size_t)vec->len < RFM_DOS_HDR_BYTES) {
        return -1;
    }
    uint8_t *p = static_cast<uint8_t *>(vector_data_or_dummy(vec));
    rfm_dos_header hdr;
    memcpy(&hdr, p, sizeof(hdr));
    if (memcmp(hdr.magic, rfm_dos_magic7, sizeof(rfm_dos_magic7)) != 0 ||
        hdr.magic[7] != RFM_DOS_RAW || hdr.nrow == 0 || hdr.ncol == 0 ||
        (uint64_t)first_col > hdr.ncol || (uint64_t)ncol > hdr.ncol - first_col ||
        hdr.nrow > (uint64_t)(std::numeric_limits<size_t>::max() -
                              RFM_DOS_HDR_BYTES) / hdr.ncol ||
        (size_t)vec->len < RFM_DOS_HDR_BYTES + (size_t)hdr.nrow * (size_t)hdr.ncol) {
        return -1;
    }
    const R_xlen_t nrow = (R_xlen_t)hdr.nrow;
    uint8_t *base = p + RFM_DOS_HDR_BYTES + first_col * nrow;
    for (R_xlen_t c = 0; c < ncol; c++) {
        uint8_t *col = base + c * nrow;
        for (R_xlen_t i = 0; i < nrow; i++) {
            const double d = src[i + c * nrow];
            if (ISNAN(d)) {
                col[i] = (uint8_t)RFM_DOS_MISSING;
            } else if (d < 0.0 || d > 2.0) {
                return -1;
            } else {
                col[i] = (uint8_t)lround(d * RFM_DOS_SCALE);
            }
        }
    }
    vec->maybe_dirty = true;
    return 0;
}

//==============================================================================
// Encode a numeric dosage matrix (values in [0, 2] or NA) into a "dosage"
// payload.
//==============================================================================
extern "C" SEXP rfm_tensor_dosage_encode_impl(SEXP x, SEXP runtime_xptr)
{
    if (TYPEOF(x) != REALSXP) {
        Rf_error("x must be a numeric matrix of dosages in [0, 2] (or NA)");
    }
    SEXP xdim = Rf_getAttrib(x, R_DimSymbol);
    if (xdim == R_NilValue || TYPEOF(xdim) != INTSXP || XLENGTH(xdim) != 2) {
        Rf_error("x must be a matrix");
    }
    const R_xlen_t nrow = (R_xlen_t)INTEGER(xdim)[0];
    const R_xlen_t ncol = (R_xlen_t)INTEGER(xdim)[1];
    SEXP ans = PROTECT(Rfmalloc_dosage_create(runtime_xptr, nrow, ncol));
    if (Rfmalloc_dosage_write(ans, 0, ncol, REAL(x)) != 0) {
        UNPROTECT(1);
        Rf_error("x contains a dosage outside [0, 2]");
    }
    UNPROTECT(1);
    return ans;
}

//==============================================================================
// Bake per-variant standardization into a dosage tensor: one streaming pass
// computes each variant's mean and sd (over observed dosages), and stores them
// so that decode returns standardized, mean-imputed values. The mirror of
// rfm_tensor_bed_standardize_impl; see fmalloc_bed.inc for the derivation.
//==============================================================================
extern "C" SEXP rfm_tensor_dosage_standardize_impl(SEXP payload, SEXP runtime_xptr,
                                                   SEXP scale_mode_sexp)
{
    fm_vector *in_vec = maybe_vector_from_altrep(payload);
    if (!in_vec || in_vec->type != RAWSXP) {
        Rf_error("x must be a 'dosage' fmalloc tensor");
    }
    fm_runtime *runtime = runtime_from_xptr(runtime_xptr);
    if (!runtime || !runtime->info) {
        Rf_error("fmalloc runtime is closed");
    }
    const int scale_mode = Rf_asInteger(scale_mode_sexp);

    const uint8_t *ip = static_cast<const uint8_t *>(vector_data_or_dummy(in_vec));
    if ((size_t)in_vec->len < RFM_DOS_HDR_BYTES) {
        Rf_error("payload too short to be a 'dosage' tensor");
    }
    rfm_dos_header hdr;
    memcpy(&hdr, ip, sizeof(hdr));
    if (memcmp(hdr.magic, rfm_dos_magic7, sizeof(rfm_dos_magic7)) != 0) {
        Rf_error("payload is not a 'dosage' tensor");
    }
    if (hdr.magic[7] != RFM_DOS_RAW) {
        Rf_error("dosage tensor is already standardized");
    }
    const R_xlen_t nrow = (R_xlen_t)hdr.nrow;
    const R_xlen_t ncol = (R_xlen_t)hdr.ncol;
    if (nrow <= 0 || ncol <= 0) {
        Rf_error("dosage tensor has no data");
    }
    const size_t body = (size_t)nrow * (size_t)ncol;
    const size_t stats_bytes = (size_t)ncol * 2 * sizeof(double);
    if ((size_t)in_vec->len < RFM_DOS_HDR_BYTES + body) {
        Rf_error("dosage payload too short for its declared dimensions");
    }

    fm_vector *out_vec = allocate_fm_vector(
        runtime, RAWSXP, (R_xlen_t)(RFM_DOS_HDR_BYTES + stats_bytes + body), true, false);
    SEXP ans = PROTECT(fmalloc_new_altrep(out_vec));
    uint8_t *op = static_cast<uint8_t *>(vector_data_or_dummy(out_vec));

    rfm_dos_header ohdr = hdr;
    ohdr.magic[7] = RFM_DOS_STD;
    memcpy(op, &ohdr, sizeof(ohdr));

    double *stats = reinterpret_cast<double *>(op + RFM_DOS_HDR_BYTES);
    const uint8_t *ibody = ip + RFM_DOS_HDR_BYTES;
    uint8_t *obody = op + RFM_DOS_HDR_BYTES + stats_bytes;
    memcpy(obody, ibody, body); // dosages travel with the tensor, verbatim

    for (R_xlen_t c = 0; c < ncol; c++) {
        const uint8_t *col = ibody + c * nrow;
        long double sum = 0.0L, sumsq = 0.0L;
        R_xlen_t n_obs = 0;
        for (R_xlen_t i = 0; i < nrow; i++) {
            const uint8_t b = col[i];
            if (b == RFM_DOS_MISSING) {
                continue;
            }
            const double d = (double)b / RFM_DOS_SCALE;
            sum += (long double)d;
            sumsq += (long double)d * (long double)d;
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
                // entries sit at the mean, so the sum of squared deviations is
                // that of the observed entries: sumsq - n_obs * mean^2.
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
