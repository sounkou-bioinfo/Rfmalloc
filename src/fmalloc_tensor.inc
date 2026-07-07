//==============================================================================
// Typed fmalloc tensors: raw fmalloc payloads carrying a foreign element
// encoding (f32, f16, bf16, quantized blocks, ...) that are decoded to
// doubles panel-by-panel at compute time instead of being materialized whole.
//
// Codecs decode a flat, block-aligned element range of a payload into
// doubles. Ranges passed to a codec always start on a block boundary; the
// element count is a block multiple except possibly on the final call for a
// payload, and codecs must write exactly n_elems doubles. Other packages
// register codecs through the Rfmalloc_register_tensor_codec C-callable.
//==============================================================================

typedef int (*rfm_tensor_decode_fn)(const void *payload, R_xlen_t elem_offset,
                                    R_xlen_t n_elems, double *out);

struct rfm_tensor_codec {
    char name[32];
    uint32_t items_per_block;
    uint32_t bytes_per_block;
    rfm_tensor_decode_fn decode;
};

#define RFM_TENSOR_MAX_CODECS 32
static rfm_tensor_codec tensor_codecs[RFM_TENSOR_MAX_CODECS];
static int tensor_codec_count = 0;

static const rfm_tensor_codec *tensor_codec_lookup(const char *name)
{
    for (int i = 0; i < tensor_codec_count; i++) {
        if (strcmp(tensor_codecs[i].name, name) == 0) {
            return &tensor_codecs[i];
        }
    }
    return nullptr;
}

extern "C" int Rfmalloc_register_tensor_codec(const char *name,
                                              uint32_t items_per_block,
                                              uint32_t bytes_per_block,
                                              rfm_tensor_decode_fn decode)
{
    if (!name || !name[0] || strlen(name) >= sizeof(tensor_codecs[0].name) ||
        items_per_block == 0 || bytes_per_block == 0 || !decode) {
        return -1;
    }
    if (tensor_codec_lookup(name)) {
        return -2;
    }
    if (tensor_codec_count >= RFM_TENSOR_MAX_CODECS) {
        return -3;
    }
    rfm_tensor_codec *codec = &tensor_codecs[tensor_codec_count];
    strcpy(codec->name, name);
    codec->items_per_block = items_per_block;
    codec->bytes_per_block = bytes_per_block;
    codec->decode = decode;
    tensor_codec_count++;
    return 0;
}

//==============================================================================
// Builtin scalar codecs
//==============================================================================

static int tensor_decode_f64(const void *payload, R_xlen_t off, R_xlen_t n, double *out)
{
    memcpy(out, static_cast<const double *>(payload) + off, (size_t)n * sizeof(double));
    return 0;
}

static int tensor_decode_f32(const void *payload, R_xlen_t off, R_xlen_t n, double *out)
{
    const float *src = static_cast<const float *>(payload) + off;
    for (R_xlen_t i = 0; i < n; i++) {
        out[i] = (double)src[i];
    }
    return 0;
}

static inline double half_bits_to_double(uint16_t h)
{
    uint32_t exp = (uint32_t)(h >> 10) & 0x1fu;
    uint32_t mant = (uint32_t)h & 0x3ffu;
    double v;
    if (exp == 0) {
        v = ldexp((double)mant, -24);
    } else if (exp == 31) {
        v = mant ? std::numeric_limits<double>::quiet_NaN()
                 : std::numeric_limits<double>::infinity();
    } else {
        v = ldexp((double)(mant | 0x400u), (int)exp - 25);
    }
    return (h & 0x8000u) ? -v : v;
}

static int tensor_decode_f16(const void *payload, R_xlen_t off, R_xlen_t n, double *out)
{
    const uint16_t *src = static_cast<const uint16_t *>(payload) + off;
    for (R_xlen_t i = 0; i < n; i++) {
        out[i] = half_bits_to_double(src[i]);
    }
    return 0;
}

static int tensor_decode_bf16(const void *payload, R_xlen_t off, R_xlen_t n, double *out)
{
    const uint16_t *src = static_cast<const uint16_t *>(payload) + off;
    for (R_xlen_t i = 0; i < n; i++) {
        uint32_t bits = (uint32_t)src[i] << 16;
        float f;
        memcpy(&f, &bits, sizeof(f));
        out[i] = (double)f;
    }
    return 0;
}

static void tensor_register_builtin_codecs(void)
{
    Rfmalloc_register_tensor_codec("f64", 1, 8, tensor_decode_f64);
    Rfmalloc_register_tensor_codec("f32", 1, 4, tensor_decode_f32);
    Rfmalloc_register_tensor_codec("f16", 1, 2, tensor_decode_f16);
    Rfmalloc_register_tensor_codec("bf16", 1, 2, tensor_decode_bf16);
}

//==============================================================================
// Shared helpers
//==============================================================================

struct rfm_tensor_source {
    const rfm_tensor_codec *codec;
    const void *payload;
    fm_runtime *runtime;
    R_xlen_t nrow;       // dims[0]; a valid matrix nrow only when ndim == 2
    R_xlen_t ncol;       // dims[1] when ndim >= 2, else 1
    R_xlen_t total_elems; // product of all dims (codecs decode a flat range)
    int ndim;
    size_t payload_bytes;
    bool fixed_geometry; // true when byte offset == block_index * bytes_per_block
};

static const rfm_tensor_codec *tensor_codec_from_sexp(SEXP codec_name)
{
    if (TYPEOF(codec_name) != STRSXP || XLENGTH(codec_name) != 1) {
        Rf_error("codec must be a single string");
    }
    const rfm_tensor_codec *codec = tensor_codec_lookup(CHAR(STRING_ELT(codec_name, 0)));
    if (!codec) {
        Rf_error("unknown fmalloc tensor codec '%s'", CHAR(STRING_ELT(codec_name, 0)));
    }
    return codec;
}

static void tensor_source_from_args(SEXP payload, SEXP codec_name, SEXP dims_sexp,
                                    rfm_tensor_source *src)
{
    fm_vector *vec = maybe_vector_from_altrep(payload);
    if (!vec || vec->type != RAWSXP) {
        Rf_error("tensor payload must be an fmalloc raw vector");
    }
    if (!vec->runtime || !vec->runtime->info) {
        Rf_error("fmalloc runtime is closed");
    }

    src->codec = tensor_codec_from_sexp(codec_name);
    src->payload = vector_data_or_dummy(vec);
    src->runtime = vec->runtime;

    if (TYPEOF(dims_sexp) != INTSXP || XLENGTH(dims_sexp) < 1) {
        Rf_error("tensor dims must be a non-empty integer vector");
    }
    // Storage/decoding is dimension-agnostic (codecs decode a flat element
    // range); only matrix products care about 2-D. Track the full shape but
    // work off the total element count.
    src->ndim = (int)XLENGTH(dims_sexp);
    R_xlen_t total = 1;
    for (int i = 0; i < src->ndim; i++) {
        R_xlen_t d = (R_xlen_t)INTEGER(dims_sexp)[i];
        if (d < 0) {
            Rf_error("tensor dims must be non-negative");
        }
        if (d != 0 && total > (R_xlen_t)std::numeric_limits<R_xlen_t>::max() / d) {
            Rf_error("tensor is too large");
        }
        total *= d;
    }
    src->total_elems = total;
    src->nrow = (R_xlen_t)INTEGER(dims_sexp)[0];
    src->ncol = src->ndim >= 2 ? (R_xlen_t)INTEGER(dims_sexp)[1] : 1;

    R_xlen_t n_elems = total;
    uint32_t ipb = src->codec->items_per_block;
    R_xlen_t n_blocks = (n_elems + ipb - 1) / ipb;
    R_xlen_t needed = n_blocks * (R_xlen_t)src->codec->bytes_per_block;
    if (vec->len < needed) {
        Rf_error("tensor payload too short: need %lld bytes for %lld '%s' elements, have %lld",
                 (long long)needed, (long long)n_elems, src->codec->name,
                 (long long)vec->len);
    }

    src->payload_bytes = (size_t)vec->len;
    // A codec has fixed geometry (block b lives at byte b*bytes_per_block, so a
    // panel's compressed byte range is computable and evictable) exactly when
    // the payload is the predicted size. Self-indexing codecs like "alp" carry
    // an offset table and store more, so this is false for them.
    src->fixed_geometry = ((size_t)needed == (size_t)vec->len);
}

// Release the compressed source pages backing a decoded element range, so a
// tensor whose payload exceeds RAM streams with a bounded resident set. Only
// valid for fixed-geometry codecs; page alignment keeps shared edge blocks.
static void tensor_evict_range(const rfm_tensor_source *src, R_xlen_t elem_off,
                               R_xlen_t n_elems)
{
    if (!src->fixed_geometry || n_elems <= 0) {
        return;
    }
    uint32_t ipb = src->codec->items_per_block;
    uint32_t bpb = src->codec->bytes_per_block;
    // Fully-covered blocks only: round the block start up, the end down.
    R_xlen_t blk_start = (elem_off + ipb - 1) / ipb;
    R_xlen_t blk_end = (elem_off + n_elems) / ipb;
    if (blk_end <= blk_start) {
        return;
    }
    size_t byte_start = (size_t)blk_start * bpb;
    size_t byte_len = (size_t)(blk_end - blk_start) * bpb;
    if (byte_start + byte_len > src->payload_bytes) {
        return;
    }
    ooc_advise(const_cast<void *>(static_cast<const void *>(
                   static_cast<const uint8_t *>(src->payload) + byte_start)),
               byte_len, OOC_DONTNEED);
}

static int tensor_decode_range(const rfm_tensor_source *src, R_xlen_t elem_off,
                               R_xlen_t n, double *out)
{
    return src->codec->decode(src->payload, elem_off, n, out);
}

static SEXP tensor_alloc_real_output(fm_runtime *runtime, R_xlen_t nrow, R_xlen_t ncol)
{
    if (nrow > 0 && ncol > 0 &&
        nrow > (std::numeric_limits<R_xlen_t>::max() / ncol)) {
        Rf_error("tensor result is too large");
    }
    fm_vector *out_vec = allocate_fm_vector(runtime, REALSXP, nrow * ncol, true, false);
    SEXP ans = PROTECT(fmalloc_new_altrep(out_vec));
    SEXP dim = PROTECT(Rf_allocVector(INTSXP, 2));
    INTEGER(dim)[0] = (int)nrow;
    INTEGER(dim)[1] = (int)ncol;
    Rf_setAttrib(ans, R_DimSymbol, dim);
    UNPROTECT(2);
    return ans;
}

static R_xlen_t tensor_panel_cols(const rfm_tensor_source *src, R_xlen_t panel_elems)
{
    // Column panels are flat payload ranges. Panel starts stay block-aligned
    // whenever the column count is a block multiple; otherwise fall back to a
    // single whole-tensor panel (offset 0 is always aligned).
    if (src->ncol == 0) {
        return 1;
    }
    if (src->nrow == 0 || (src->nrow % src->codec->items_per_block) != 0) {
        return src->ncol;
    }
    R_xlen_t cols = panel_elems / src->nrow;
    if (cols < 1) {
        cols = 1;
    }
    if (cols > src->ncol) {
        cols = src->ncol;
    }
    return cols;
}

//==============================================================================
// Panel-streaming matmul: one typed operand, one dense double operand
//==============================================================================

extern "C" SEXP rfm_tensor_matmul_impl(SEXP payload, SEXP codec_name, SEXP dims_sexp,
                                       SEXP dense, SEXP typed_on_left_sexp,
                                       SEXP panel_elems_sexp, SEXP ooc_sexp)
{
    rfm_tensor_source src;
    tensor_source_from_args(payload, codec_name, dims_sexp, &src);
    if (src.ndim != 2) {
        Rf_error("matrix product requires a 2-dimensional tensor");
    }

    bool ooc = Rf_asLogical(ooc_sexp) == TRUE;
    if (ooc && src.payload) {
        ooc_advise(const_cast<void *>(src.payload), src.payload_bytes, OOC_SEQUENTIAL);
    }

    if (TYPEOF(dense) != REALSXP) {
        Rf_error("dense operand must be a double matrix");
    }
    SEXP ddim = Rf_getAttrib(dense, R_DimSymbol);
    if (ddim == R_NilValue || TYPEOF(ddim) != INTSXP || XLENGTH(ddim) != 2) {
        Rf_error("dense operand must have matrix dims");
    }
    R_xlen_t dnrow = (R_xlen_t)INTEGER(ddim)[0];
    R_xlen_t dncol = (R_xlen_t)INTEGER(ddim)[1];

    bool typed_on_left = Rf_asLogical(typed_on_left_sexp) == TRUE;
    double panel_req = Rf_asReal(panel_elems_sexp);
    R_xlen_t panel_elems = (!R_FINITE(panel_req) || panel_req < 1) ? (R_xlen_t)1 << 23
                                                                   : (R_xlen_t)panel_req;

    R_xlen_t out_nrow, out_ncol;
    if (typed_on_left) {
        if (src.ncol != dnrow) {
            Rf_error("non-conformable arguments");
        }
        out_nrow = src.nrow;
        out_ncol = dncol;
    } else {
        if (dncol != src.nrow) {
            Rf_error("non-conformable arguments");
        }
        out_nrow = dnrow;
        out_ncol = src.ncol;
    }
    if (out_nrow > (R_xlen_t)std::numeric_limits<int>::max() ||
        out_ncol > (R_xlen_t)std::numeric_limits<int>::max() ||
        src.nrow > (R_xlen_t)std::numeric_limits<int>::max() ||
        src.ncol > (R_xlen_t)std::numeric_limits<int>::max()) {
        Rf_error("tensor dimensions exceed the supported matrix interface");
    }

    SEXP ans = PROTECT(tensor_alloc_real_output(src.runtime, out_nrow, out_ncol));
    R_xlen_t out_len = out_nrow * out_ncol;
    if (out_len == 0) {
        UNPROTECT(1);
        return ans;
    }
    double *out = REAL(ans);

    // Inner dimension of zero: dgemm needs lda >= 1, just zero the result.
    R_xlen_t inner = typed_on_left ? src.ncol : dnrow == 0 ? 0 : src.nrow;
    if (typed_on_left ? (src.ncol == 0) : (src.nrow == 0)) {
        memset(out, 0, (size_t)out_len * sizeof(double));
        UNPROTECT(1);
        return ans;
    }
    (void)inner;

    // Codec-aware backend: hand the raw compressed payload straight to the
    // active backend (e.g. a GPU/quantized engine) so it can multiply without
    // Rfmalloc decoding to f64. If it handles the product, we are done.
    if (rfm_typed_gemm(src.codec->name, src.payload, src.payload_bytes,
                       (int)src.nrow, (int)src.ncol, typed_on_left ? 1 : 0,
                       REAL(dense), (int)dnrow, (int)dncol, out)) {
        UNPROTECT(1);
        return ans;
    }

    R_xlen_t panel_cols = tensor_panel_cols(&src, panel_elems);
    std::vector<double> scratch((size_t)src.nrow * (size_t)panel_cols);
    const double one = 1.0, zero = 0.0;

    if (typed_on_left) {
        // C(tr x dc) = sum over k-panels: T[, p0:p1) x D[p0:p1, ]
        int m = (int)src.nrow, n = (int)dncol;
        for (R_xlen_t p0 = 0; p0 < src.ncol; p0 += panel_cols) {
            R_xlen_t kb = src.ncol - p0 < panel_cols ? src.ncol - p0 : panel_cols;
            if (tensor_decode_range(&src, p0 * src.nrow, kb * src.nrow, scratch.data()) != 0) {
                Rf_error("fmalloc tensor codec '%s' failed to decode", src.codec->name);
            }
            int k = (int)kb;
            int lda = m;
            int ldb = (int)dnrow;
            const double *beta = (p0 == 0) ? &zero : &one;
            F77_CALL(dgemm)("N", "N", &m, &n, &k, &one, scratch.data(), &lda,
                            REAL(dense) + p0, &ldb, beta, out, &m FCONE FCONE);
            if (ooc) {
                tensor_evict_range(&src, p0 * src.nrow, kb * src.nrow);
            }
            R_CheckUserInterrupt();
        }
    } else {
        // C[, j0:j1) = D x T[, j0:j1) — no accumulation across panels.
        int m = (int)dnrow, k = (int)src.nrow;
        for (R_xlen_t j0 = 0; j0 < src.ncol; j0 += panel_cols) {
            R_xlen_t jb = src.ncol - j0 < panel_cols ? src.ncol - j0 : panel_cols;
            if (tensor_decode_range(&src, j0 * src.nrow, jb * src.nrow, scratch.data()) != 0) {
                Rf_error("fmalloc tensor codec '%s' failed to decode", src.codec->name);
            }
            int n = (int)jb;
            int lda = m;
            int ldb = k;
            F77_CALL(dgemm)("N", "N", &m, &n, &k, &one, REAL(dense), &lda,
                            scratch.data(), &ldb, &zero, out + j0 * dnrow, &m FCONE FCONE);
            if (ooc) {
                tensor_evict_range(&src, j0 * src.nrow, jb * src.nrow);
            }
            R_CheckUserInterrupt();
        }
    }

    UNPROTECT(1);
    return ans;
}

//==============================================================================
// Materialize a typed tensor as an fmalloc double matrix
//==============================================================================

extern "C" SEXP rfm_tensor_materialize_impl(SEXP payload, SEXP codec_name, SEXP dims_sexp)
{
    rfm_tensor_source src;
    tensor_source_from_args(payload, codec_name, dims_sexp, &src);

    // Decode the whole tensor flat (any number of dimensions); the R wrapper
    // applies the shape and array/matrix class.
    fm_vector *out_vec = allocate_fm_vector(src.runtime, REALSXP, src.total_elems, true, false);
    SEXP ans = PROTECT(fmalloc_new_altrep(out_vec));
    if (src.total_elems > 0) {
        if (tensor_decode_range(&src, 0, src.total_elems, REAL(ans)) != 0) {
            Rf_error("fmalloc tensor codec '%s' failed to decode", src.codec->name);
        }
    }
    UNPROTECT(1);
    return ans;
}

//==============================================================================
// Codec introspection for the R layer
//==============================================================================

extern "C" SEXP rfm_tensor_codec_info_impl(SEXP codec_name)
{
    if (TYPEOF(codec_name) != STRSXP || XLENGTH(codec_name) != 1) {
        Rf_error("codec must be a single string");
    }
    const rfm_tensor_codec *codec = tensor_codec_lookup(CHAR(STRING_ELT(codec_name, 0)));
    if (!codec) {
        return R_NilValue;
    }
    SEXP info = PROTECT(Rf_allocVector(VECSXP, 3));
    SEXP names = PROTECT(Rf_allocVector(STRSXP, 3));
    SET_STRING_ELT(names, 0, Rf_mkChar("name"));
    SET_STRING_ELT(names, 1, Rf_mkChar("items_per_block"));
    SET_STRING_ELT(names, 2, Rf_mkChar("bytes_per_block"));
    Rf_setAttrib(info, R_NamesSymbol, names);
    SET_VECTOR_ELT(info, 0, Rf_mkString(codec->name));
    SET_VECTOR_ELT(info, 1, Rf_ScalarInteger((int)codec->items_per_block));
    SET_VECTOR_ELT(info, 2, Rf_ScalarInteger((int)codec->bytes_per_block));
    UNPROTECT(2);
    return info;
}

extern "C" SEXP rfm_tensor_codec_list_impl(void)
{
    SEXP ans = PROTECT(Rf_allocVector(STRSXP, tensor_codec_count));
    for (int i = 0; i < tensor_codec_count; i++) {
        SET_STRING_ELT(ans, i, Rf_mkChar(tensor_codecs[i].name));
    }
    UNPROTECT(1);
    return ans;
}
