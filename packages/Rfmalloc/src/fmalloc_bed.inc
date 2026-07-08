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
static const uint8_t rfm_bed_magic[8] = {'F', 'M', 'B', 'E', 'D', 0, 0, 1};

// PLINK code -> dosage of the first .bim allele. Index is the 2-bit code.
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

// Decode a column-aligned element range. Returns non-zero on a malformed
// payload or a range that straddles a column, which the tensor matmul never
// produces but a direct caller could.
static int tensor_decode_bed(const void *payload, R_xlen_t elem_offset,
                             R_xlen_t n_elems, double *out)
{
    if (!payload || n_elems < 0) {
        return -1;
    }
    rfm_bed_header hdr;
    memcpy(&hdr, payload, sizeof(hdr));
    if (memcmp(hdr.magic, rfm_bed_magic, sizeof(rfm_bed_magic)) != 0) {
        return -1;
    }
    const R_xlen_t nrow = (R_xlen_t)hdr.nrow;
    if (nrow <= 0) {
        return -1;
    }
    if (elem_offset % nrow != 0 || n_elems % nrow != 0) {
        return -1; // whole columns only
    }

    const R_xlen_t cb = rfm_bed_col_bytes(nrow);
    const uint8_t *base = static_cast<const uint8_t *>(payload) + RFM_BED_HDR_BYTES;
    const R_xlen_t c0 = elem_offset / nrow;
    const R_xlen_t nc = n_elems / nrow;

    for (R_xlen_t c = 0; c < nc; c++) {
        const uint8_t *col = base + (c0 + c) * cb;
        double *dst = out + c * nrow;
        R_xlen_t i = 0;
        // Whole bytes: four genotypes each, low pair first.
        for (; i + 4 <= nrow; i += 4) {
            const uint8_t b = col[i >> 2];
            dst[i]     = rfm_bed_lut[b & 3];
            dst[i + 1] = rfm_bed_lut[(b >> 2) & 3];
            dst[i + 2] = rfm_bed_lut[(b >> 4) & 3];
            dst[i + 3] = rfm_bed_lut[(b >> 6) & 3];
        }
        // Tail of a variant whose sample count is not a multiple of four; the
        // remaining high-order pairs of the last byte are padding.
        for (; i < nrow; i++) {
            dst[i] = rfm_bed_lut[(col[i >> 2] >> (2 * (i & 3))) & 3];
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

//==============================================================================
// Encode an integer dosage matrix (0/1/2/NA) into a "bed" payload, so tests and
// callers can build genotype tensors without a real .bed on disk. Importing an
// actual .bed is a byte copy of its body behind this header (see fmalloc_bed()).
//==============================================================================
extern "C" SEXP rfm_tensor_bed_encode_impl(SEXP x, SEXP runtime_xptr)
{
    fm_runtime *runtime = runtime_from_xptr(runtime_xptr);
    if (!runtime || !runtime->info) {
        Rf_error("fmalloc runtime is closed");
    }
    if (TYPEOF(x) != INTSXP) {
        Rf_error("x must be an integer matrix of dosages (0, 1, 2, NA)");
    }
    SEXP xdim = Rf_getAttrib(x, R_DimSymbol);
    if (xdim == R_NilValue || TYPEOF(xdim) != INTSXP || XLENGTH(xdim) != 2) {
        Rf_error("x must be a matrix");
    }
    const R_xlen_t nrow = (R_xlen_t)INTEGER(xdim)[0];
    const R_xlen_t ncol = (R_xlen_t)INTEGER(xdim)[1];
    if (nrow <= 0 || ncol <= 0) {
        Rf_error("x must have at least one row and one column");
    }

    const R_xlen_t cb = rfm_bed_col_bytes(nrow);
    const R_xlen_t body = ncol * cb;
    fm_vector *vec = allocate_fm_vector(runtime, RAWSXP, RFM_BED_HDR_BYTES + body,
                                        true, false);
    SEXP ans = PROTECT(fmalloc_new_altrep(vec));
    uint8_t *p = static_cast<uint8_t *>(vector_data_or_dummy(vec));

    rfm_bed_header hdr;
    memcpy(hdr.magic, rfm_bed_magic, sizeof(rfm_bed_magic));
    hdr.nrow = (uint64_t)nrow;
    hdr.ncol = (uint64_t)ncol;
    memcpy(p, &hdr, sizeof(hdr));

    uint8_t *base = p + RFM_BED_HDR_BYTES;
    memset(base, 0, (size_t)body);
    const int *src = INTEGER(x);
    for (R_xlen_t c = 0; c < ncol; c++) {
        uint8_t *col = base + c * cb;
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
                UNPROTECT(1);
                Rf_error("dosage must be 0, 1, 2 or NA, got %d", g);
            }
            col[i >> 2] |= (uint8_t)(code << (2 * (i & 3)));
        }
        R_CheckUserInterrupt();
    }
    vec->maybe_dirty = true;
    UNPROTECT(1);
    return ans;
}
