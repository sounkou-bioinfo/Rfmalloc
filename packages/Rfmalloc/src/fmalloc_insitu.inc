//==============================================================================
// In-place (by-reference) mutation of fmalloc atomic vectors.
//
// These routines write straight through the backing store, deliberately
// bypassing R's copy-on-modify. That is unsafe in the value-semantics sense
// (`y <- x; fmalloc_set(x, 1, 5)` also changes `y`, since both name the same
// backing store) but is exactly what large/persistent fmalloc vectors need:
// duplicating a larger-than-RAM vector on every element write is fatal, and
// for a persistent runtime an in-place write updates the durable store.
// Supported for fixed-width atomic types (logical, integer, numeric, complex,
// raw); character and list storage are offset-indirected and not writable
// this way.
//==============================================================================

static fm_vector *insitu_writable_vector(SEXP x)
{
    fm_vector *vec = maybe_vector_from_altrep(x);
    if (!vec) {
        Rf_error("x must be an fmalloc vector");
    }
    if (!vec->runtime || !vec->runtime->info) {
        Rf_error("fmalloc runtime is closed");
    }
    switch (vec->type) {
    case LGLSXP:
    case INTSXP:
    case REALSXP:
    case CPLXSXP:
    case RAWSXP:
        break;
    default:
        Rf_error("in-place mutation supports only atomic logical/integer/numeric/complex/raw vectors");
    }
    return vec;
}

// Write coerced[vi] into element `pos` of the vector's payload.
static inline void insitu_put(void *data, SEXPTYPE type, R_xlen_t pos,
                              SEXP coerced, R_xlen_t vi)
{
    switch (type) {
    case LGLSXP:  static_cast<int *>(data)[pos] = LOGICAL(coerced)[vi]; break;
    case INTSXP:  static_cast<int *>(data)[pos] = INTEGER(coerced)[vi]; break;
    case REALSXP: static_cast<double *>(data)[pos] = REAL(coerced)[vi]; break;
    case CPLXSXP: static_cast<Rcomplex *>(data)[pos] = COMPLEX(coerced)[vi]; break;
    case RAWSXP:  static_cast<Rbyte *>(data)[pos] = RAW(coerced)[vi]; break;
    default: break;
    }
}

// x[i] <- values, in place. i is 1-based positive indices; values has length 1
// (recycled) or length(i). Returns x invisibly (at the R level).
extern "C" SEXP rfm_set_in_place_impl(SEXP x, SEXP idx_sexp, SEXP values_sexp)
{
    fm_vector *vec = insitu_writable_vector(x);

    SEXP idx = PROTECT(Rf_coerceVector(idx_sexp, REALSXP));
    R_xlen_t nidx = XLENGTH(idx);
    R_xlen_t nval = XLENGTH(values_sexp);
    if (nval != 1 && nval != nidx) {
        UNPROTECT(1);
        Rf_error("values must have length 1 or length(i)");
    }

    SEXP coerced = PROTECT(Rf_coerceVector(values_sexp, vec->type));
    const double *ip = REAL(idx);
    void *data = vector_data_or_dummy(vec);
    R_xlen_t len = vec->len;

    for (R_xlen_t k = 0; k < nidx; k++) {
        double di = ip[k];
        if (!R_FINITE(di) || di < 1 || di > (double)len) {
            UNPROTECT(2);
            Rf_error("index %g is out of bounds [1, %lld]", di, (long long)len);
        }
        R_xlen_t pos = (R_xlen_t)di - 1;
        insitu_put(data, vec->type, pos, coerced, nval == 1 ? 0 : k);
    }

    vec->maybe_dirty = true;
    UNPROTECT(2);
    return x;
}

// x <- x op y, in place, for a numeric (double) fmalloc vector. y is a scalar
// (recycled) or length(x). op: 0 add, 1 sub, 2 mul, 3 div. NA/NaN/Inf follow
// IEEE double arithmetic, matching base R.
extern "C" SEXP rfm_inplace_op_impl(SEXP x, SEXP y_sexp, SEXP op_sexp)
{
    fm_vector *vec = insitu_writable_vector(x);
    if (vec->type != REALSXP) {
        Rf_error("in-place arithmetic requires a numeric (double) fmalloc vector");
    }
    int op = Rf_asInteger(op_sexp);
    if (op < 0 || op > 3) {
        Rf_error("invalid in-place arithmetic op");
    }

    SEXP y = PROTECT(Rf_coerceVector(y_sexp, REALSXP));
    R_xlen_t ny = XLENGTH(y);
    R_xlen_t len = vec->len;
    if (ny != 1 && ny != len) {
        UNPROTECT(1);
        Rf_error("y must have length 1 or length(x)");
    }

    double *d = static_cast<double *>(vector_data_or_dummy(vec));
    const double *yp = REAL(y);
    bool scalar = (ny == 1);
    double s = scalar ? yp[0] : 0.0;

    switch (op) {
    case 0: for (R_xlen_t i = 0; i < len; i++) d[i] += scalar ? s : yp[i]; break;
    case 1: for (R_xlen_t i = 0; i < len; i++) d[i] -= scalar ? s : yp[i]; break;
    case 2: for (R_xlen_t i = 0; i < len; i++) d[i] *= scalar ? s : yp[i]; break;
    case 3: for (R_xlen_t i = 0; i < len; i++) d[i] /= scalar ? s : yp[i]; break;
    default: break;
    }

    vec->maybe_dirty = true;
    UNPROTECT(1);
    return x;
}

// x[] <- value, in place: fill the whole vector with a scalar.
extern "C" SEXP rfm_fill_in_place_impl(SEXP x, SEXP value_sexp)
{
    fm_vector *vec = insitu_writable_vector(x);
    if (XLENGTH(value_sexp) != 1) {
        Rf_error("value must be a scalar");
    }

    SEXP coerced = PROTECT(Rf_coerceVector(value_sexp, vec->type));
    void *data = vector_data_or_dummy(vec);
    R_xlen_t len = vec->len;

    switch (vec->type) {
    case LGLSXP: case INTSXP: {
        int v = INTEGER(coerced)[0];
        int *d = static_cast<int *>(data);
        for (R_xlen_t i = 0; i < len; i++) d[i] = v;
        break;
    }
    case REALSXP: {
        double v = REAL(coerced)[0];
        double *d = static_cast<double *>(data);
        for (R_xlen_t i = 0; i < len; i++) d[i] = v;
        break;
    }
    case CPLXSXP: {
        Rcomplex v = COMPLEX(coerced)[0];
        Rcomplex *d = static_cast<Rcomplex *>(data);
        for (R_xlen_t i = 0; i < len; i++) d[i] = v;
        break;
    }
    case RAWSXP: {
        Rbyte v = RAW(coerced)[0];
        memset(data, v, (size_t)len);
        break;
    }
    default: break;
    }

    vec->maybe_dirty = true;
    UNPROTECT(1);
    return x;
}
