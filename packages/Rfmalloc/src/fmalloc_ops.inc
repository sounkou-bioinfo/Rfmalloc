//==============================================================================
// Native C/C++ Ops implementation
//==============================================================================

#include "fmalloc_ops.h"

//==============================================================================
// Struct definitions (R-typed, so must be inside .inc after R headers)
//==============================================================================

struct fm_source {
    fm_type_id type;
    void *data;
    R_xlen_t len;
    size_t elt_size;
    fm_runtime *runtime;
    fm_vector *vector;
    SEXP sexp_keepalive;
    bool is_fmalloc;
    bool is_scalar;
};

struct fm_op_plan {
    fm_op_id op;
    bool unary;
    fm_source lhs;
    fm_source rhs;
    fm_runtime *out_runtime;
    SEXPTYPE out_sexptype;
    fm_type_id out_type;
    R_xlen_t out_len;
    bool recycle_lhs;
    bool recycle_rhs;
    bool warn_recycling;
    SEXP out_dim;
    SEXP out_dimnames;
};

//==============================================================================
// Forward declarations of execution functions
//==============================================================================

static SEXP execute_plan(fm_op_plan *plan);

static void exec_binary_out_int(
    fm_source *lhs, fm_source *rhs, const fm_op_plan *plan,
    R_xlen_t start, R_xlen_t n, int *out, int (*op)(int, int));

static void exec_binary_out_int_mixed(
    fm_source *lhs, fm_source *rhs, const fm_op_plan *plan,
    R_xlen_t start, R_xlen_t n, int *out, int (*op)(int, double));

static void exec_binary_out_int_mixed2(
    fm_source *lhs, fm_source *rhs, const fm_op_plan *plan,
    R_xlen_t start, R_xlen_t n, int *out, int (*op)(double, int));

static void exec_binary_out_double(
    fm_source *lhs, fm_source *rhs, const fm_op_plan *plan,
    R_xlen_t start, R_xlen_t n, double *out, double (*op)(double, double));

static void exec_binary_out_double_int_int(
    fm_source *lhs, fm_source *rhs, const fm_op_plan *plan,
    R_xlen_t start, R_xlen_t n, double *out, double (*op)(int, int));

static void exec_binary_out_double_mixed(
    fm_source *lhs, fm_source *rhs, const fm_op_plan *plan,
    R_xlen_t start, R_xlen_t n, double *out, double (*op)(int, double));

static void exec_binary_out_double_mixed2(
    fm_source *lhs, fm_source *rhs, const fm_op_plan *plan,
    R_xlen_t start, R_xlen_t n, double *out, double (*op)(double, int));

static void exec_unary_out_int(
    fm_source *src, const fm_op_plan *plan,
    R_xlen_t start, R_xlen_t n, int *out, int (*op)(int));

static void exec_unary_out_double(
    fm_source *src, const fm_op_plan *plan,
    R_xlen_t start, R_xlen_t n, double *out, double (*op)(double));

static void exec_binary_real_to_logical(
    fm_source *lhs, fm_source *rhs, const fm_op_plan *plan,
    R_xlen_t start, R_xlen_t n, int *out, int (*op)(double, double));

//==============================================================================
// Element-level operation functions
//==============================================================================

static int e_add_ii(int a, int b) {
    if (a == NA_INTEGER || b == NA_INTEGER) return NA_INTEGER;
    return a + b;
}
static int e_sub_ii(int a, int b) {
    if (a == NA_INTEGER || b == NA_INTEGER) return NA_INTEGER;
    return a - b;
}
static int e_mul_ii(int a, int b) {
    if (a == NA_INTEGER || b == NA_INTEGER) return NA_INTEGER;
    return a * b;
}
static int e_idiv_ii(int a, int b) {
    if (a == NA_INTEGER || b == NA_INTEGER) return NA_INTEGER;
    return b == 0 ? NA_INTEGER : a / b;
}
static int e_mod_ii(int a, int b) {
    if (a == NA_INTEGER || b == NA_INTEGER) return NA_INTEGER;
    return b == 0 ? NA_INTEGER : a % b;
}
static double e_div_ii(int a, int b) {
    if (a == NA_INTEGER || b == NA_INTEGER) return NA_REAL;
    return (double)a / (double)b;
}

static double e_add_dd(double a, double b) { return a + b; }
static double e_sub_dd(double a, double b) { return a - b; }
static double e_mul_dd(double a, double b) { return a * b; }
static double e_div_dd(double a, double b) { return a / b; }
static double e_pow_dd(double a, double b) {
    if (ISNA(a) || ISNA(b)) return NA_REAL;
    return pow(a, b);
}
static double e_pow_ii(int a, int b) {
    if (a == NA_INTEGER || b == NA_INTEGER) return NA_REAL;
    if (b < 0) return NA_REAL;
    return pow((double)a, (double)b);
}
static double e_mod_dd(double a, double b) {
    if (b == 0.0 || ISNA(a) || ISNA(b)) return NA_REAL;
    return fmod(a, b);
}

static double e_add_id(int a, double b) {
    if (a == NA_INTEGER || ISNA(b)) return NA_REAL; return (double)a + b;
}
static double e_add_di(double a, int b) {
    if (ISNA(a) || b == NA_INTEGER) return NA_REAL; return a + (double)b;
}
static double e_sub_id(int a, double b) {
    if (a == NA_INTEGER || ISNA(b)) return NA_REAL; return (double)a - b;
}
static double e_sub_di(double a, int b) {
    if (ISNA(a) || b == NA_INTEGER) return NA_REAL; return a - (double)b;
}
static double e_mul_id(int a, double b) {
    if (a == NA_INTEGER || ISNA(b)) return NA_REAL; return (double)a * b;
}
static double e_mul_di(double a, int b) {
    if (ISNA(a) || b == NA_INTEGER) return NA_REAL; return a * (double)b;
}
static double e_div_id(int a, double b) {
    if (a == NA_INTEGER) return NA_REAL;
    return (double)a / b;
}
static double e_div_di(double a, int b) {
    if (b == NA_INTEGER) return NA_REAL;
    return a / (double)b;
}
static double e_pow_id(int a, double b) {
    if (a == NA_INTEGER || ISNA(b)) return NA_REAL; return pow((double)a, b);
}
static double e_pow_di(double a, int b) {
    if (ISNA(a) || b == NA_INTEGER) return NA_REAL; return pow(a, (double)b);
}

static int e_eq_ii(int a, int b) {
    return (a == NA_INTEGER || b == NA_INTEGER) ? NA_INTEGER : (a == b ? 1 : 0);
}
static int e_ne_ii(int a, int b) {
    return (a == NA_INTEGER || b == NA_INTEGER) ? NA_INTEGER : (a != b ? 1 : 0);
}
static int e_lt_ii(int a, int b) {
    return (a == NA_INTEGER || b == NA_INTEGER) ? NA_INTEGER : (a < b ? 1 : 0);
}
static int e_le_ii(int a, int b) {
    return (a == NA_INTEGER || b == NA_INTEGER) ? NA_INTEGER : (a <= b ? 1 : 0);
}
static int e_gt_ii(int a, int b) {
    return (a == NA_INTEGER || b == NA_INTEGER) ? NA_INTEGER : (a > b ? 1 : 0);
}
static int e_ge_ii(int a, int b) {
    return (a == NA_INTEGER || b == NA_INTEGER) ? NA_INTEGER : (a >= b ? 1 : 0);
}

static int e_eq_dd(double a, double b) {
    return (ISNA(a) || ISNA(b)) ? NA_INTEGER : (a == b ? 1 : 0);
}
static int e_ne_dd(double a, double b) {
    return (ISNA(a) || ISNA(b)) ? NA_INTEGER : (a != b ? 1 : 0);
}
static int e_lt_dd(double a, double b) {
    return (ISNA(a) || ISNA(b)) ? NA_INTEGER : (a < b ? 1 : 0);
}
static int e_le_dd(double a, double b) {
    return (ISNA(a) || ISNA(b)) ? NA_INTEGER : (a <= b ? 1 : 0);
}
static int e_gt_dd(double a, double b) {
    return (ISNA(a) || ISNA(b)) ? NA_INTEGER : (a > b ? 1 : 0);
}
static int e_ge_dd(double a, double b) {
    return (ISNA(a) || ISNA(b)) ? NA_INTEGER : (a >= b ? 1 : 0);
}

static int e_eq_id(int a, double b) {
    if (a == NA_INTEGER || ISNA(b)) return NA_INTEGER;
    return (double)a == b ? 1 : 0;
}
static int e_eq_di(double a, int b) {
    if (ISNA(a) || b == NA_INTEGER) return NA_INTEGER;
    return a == (double)b ? 1 : 0;
}
static int e_ne_id(int a, double b) { int r = e_eq_id(a, b); return r == NA_INTEGER ? NA_INTEGER : !r; }
static int e_ne_di(double a, int b) { int r = e_eq_di(a, b); return r == NA_INTEGER ? NA_INTEGER : !r; }
static int e_lt_id(int a, double b) { return (a == NA_INTEGER || ISNA(b)) ? NA_INTEGER : ((double)a < b ? 1 : 0); }
static int e_lt_di(double a, int b) { return (ISNA(a) || b == NA_INTEGER) ? NA_INTEGER : (a < (double)b ? 1 : 0); }
static int e_le_id(int a, double b) { return (a == NA_INTEGER || ISNA(b)) ? NA_INTEGER : ((double)a <= b ? 1 : 0); }
static int e_le_di(double a, int b) { return (ISNA(a) || b == NA_INTEGER) ? NA_INTEGER : (a <= (double)b ? 1 : 0); }
static int e_gt_id(int a, double b) { return (a == NA_INTEGER || ISNA(b)) ? NA_INTEGER : ((double)a > b ? 1 : 0); }
static int e_gt_di(double a, int b) { return (ISNA(a) || b == NA_INTEGER) ? NA_INTEGER : (a > (double)b ? 1 : 0); }
static int e_ge_id(int a, double b) { return (a == NA_INTEGER || ISNA(b)) ? NA_INTEGER : ((double)a >= b ? 1 : 0); }
static int e_ge_di(double a, int b) { return (ISNA(a) || b == NA_INTEGER) ? NA_INTEGER : (a >= (double)b ? 1 : 0); }

static int e_ll_and(int a, int b) {
    if ((a != NA_INTEGER && a == 0) || (b != NA_INTEGER && b == 0)) return 0;
    if (a == NA_INTEGER || b == NA_INTEGER) return NA_INTEGER;
    return 1;
}
static int e_ll_or(int a, int b) {
    if ((a != NA_INTEGER && a != 0) || (b != NA_INTEGER && b != 0)) return 1;
    if (a == NA_INTEGER || b == NA_INTEGER) return NA_INTEGER;
    return 0;
}
static int e_ll_eq(int a, int b) {
    if (a == NA_INTEGER || b == NA_INTEGER) return NA_INTEGER;
    return (a == b) ? 1 : 0;
}
static int e_ll_ne(int a, int b) {
    if (a == NA_INTEGER || b == NA_INTEGER) return NA_INTEGER;
    return (a != b) ? 1 : 0;
}
static int e_not_l(int a) {
    return a == NA_INTEGER ? NA_INTEGER : (a == 0 ? 1 : 0);
}

static int e_pos_i(int a) { return a; }
static int e_neg_i(int a) { return a == NA_INTEGER ? NA_INTEGER : -a; }
static double e_pos_d(double a) { return a; }
static double e_neg_d(double a) { return ISNA(a) ? NA_REAL : -a; }

//==============================================================================
// Type resolution table
//==============================================================================

struct fm_type_rule {
    fm_op_id op;
    fm_type_id lhs;
    fm_type_id rhs;
    fm_type_id out;
    int out_sexptype;
};

static const fm_type_rule FM_TYPE_RULES[] = {
    // int arithmetic → int
    {FM_OP_ADD, FM_T_INTEGER, FM_T_INTEGER, FM_T_INTEGER, INTSXP},
    {FM_OP_SUB, FM_T_INTEGER, FM_T_INTEGER, FM_T_INTEGER, INTSXP},
    {FM_OP_MUL, FM_T_INTEGER, FM_T_INTEGER, FM_T_INTEGER, INTSXP},
    {FM_OP_IDIV, FM_T_INTEGER, FM_T_INTEGER, FM_T_INTEGER, INTSXP},
    {FM_OP_MOD, FM_T_INTEGER, FM_T_INTEGER, FM_T_INTEGER, INTSXP},

    // int div/pow → real
    {FM_OP_DIV, FM_T_INTEGER, FM_T_INTEGER, FM_T_REAL, REALSXP},
    {FM_OP_POW, FM_T_INTEGER, FM_T_INTEGER, FM_T_REAL, REALSXP},

    // real arithmetic → real
    {FM_OP_ADD, FM_T_REAL, FM_T_REAL, FM_T_REAL, REALSXP},
    {FM_OP_SUB, FM_T_REAL, FM_T_REAL, FM_T_REAL, REALSXP},
    {FM_OP_MUL, FM_T_REAL, FM_T_REAL, FM_T_REAL, REALSXP},
    {FM_OP_DIV, FM_T_REAL, FM_T_REAL, FM_T_REAL, REALSXP},
    {FM_OP_POW, FM_T_REAL, FM_T_REAL, FM_T_REAL, REALSXP},
    {FM_OP_MOD, FM_T_REAL, FM_T_REAL, FM_T_REAL, REALSXP},

    // int + real → real
    {FM_OP_ADD, FM_T_INTEGER, FM_T_REAL, FM_T_REAL, REALSXP},
    {FM_OP_SUB, FM_T_INTEGER, FM_T_REAL, FM_T_REAL, REALSXP},
    {FM_OP_MUL, FM_T_INTEGER, FM_T_REAL, FM_T_REAL, REALSXP},
    {FM_OP_DIV, FM_T_INTEGER, FM_T_REAL, FM_T_REAL, REALSXP},
    {FM_OP_POW, FM_T_INTEGER, FM_T_REAL, FM_T_REAL, REALSXP},

    // real + int → real
    {FM_OP_ADD, FM_T_REAL, FM_T_INTEGER, FM_T_REAL, REALSXP},
    {FM_OP_SUB, FM_T_REAL, FM_T_INTEGER, FM_T_REAL, REALSXP},
    {FM_OP_MUL, FM_T_REAL, FM_T_INTEGER, FM_T_REAL, REALSXP},
    {FM_OP_DIV, FM_T_REAL, FM_T_INTEGER, FM_T_REAL, REALSXP},
    {FM_OP_POW, FM_T_REAL, FM_T_INTEGER, FM_T_REAL, REALSXP},

    // int comparisons → logical
    {FM_OP_EQ, FM_T_INTEGER, FM_T_INTEGER, FM_T_LOGICAL, LGLSXP},
    {FM_OP_NE, FM_T_INTEGER, FM_T_INTEGER, FM_T_LOGICAL, LGLSXP},
    {FM_OP_LT, FM_T_INTEGER, FM_T_INTEGER, FM_T_LOGICAL, LGLSXP},
    {FM_OP_LE, FM_T_INTEGER, FM_T_INTEGER, FM_T_LOGICAL, LGLSXP},
    {FM_OP_GT, FM_T_INTEGER, FM_T_INTEGER, FM_T_LOGICAL, LGLSXP},
    {FM_OP_GE, FM_T_INTEGER, FM_T_INTEGER, FM_T_LOGICAL, LGLSXP},

    // real comparisons → logical
    {FM_OP_EQ, FM_T_REAL, FM_T_REAL, FM_T_LOGICAL, LGLSXP},
    {FM_OP_NE, FM_T_REAL, FM_T_REAL, FM_T_LOGICAL, LGLSXP},
    {FM_OP_LT, FM_T_REAL, FM_T_REAL, FM_T_LOGICAL, LGLSXP},
    {FM_OP_LE, FM_T_REAL, FM_T_REAL, FM_T_LOGICAL, LGLSXP},
    {FM_OP_GT, FM_T_REAL, FM_T_REAL, FM_T_LOGICAL, LGLSXP},
    {FM_OP_GE, FM_T_REAL, FM_T_REAL, FM_T_LOGICAL, LGLSXP},

    // mixed comparisons → logical
    {FM_OP_EQ, FM_T_INTEGER, FM_T_REAL, FM_T_LOGICAL, LGLSXP},
    {FM_OP_NE, FM_T_INTEGER, FM_T_REAL, FM_T_LOGICAL, LGLSXP},
    {FM_OP_LT, FM_T_INTEGER, FM_T_REAL, FM_T_LOGICAL, LGLSXP},
    {FM_OP_LE, FM_T_INTEGER, FM_T_REAL, FM_T_LOGICAL, LGLSXP},
    {FM_OP_GT, FM_T_INTEGER, FM_T_REAL, FM_T_LOGICAL, LGLSXP},
    {FM_OP_GE, FM_T_INTEGER, FM_T_REAL, FM_T_LOGICAL, LGLSXP},

    {FM_OP_EQ, FM_T_REAL, FM_T_INTEGER, FM_T_LOGICAL, LGLSXP},
    {FM_OP_NE, FM_T_REAL, FM_T_INTEGER, FM_T_LOGICAL, LGLSXP},
    {FM_OP_LT, FM_T_REAL, FM_T_INTEGER, FM_T_LOGICAL, LGLSXP},
    {FM_OP_LE, FM_T_REAL, FM_T_INTEGER, FM_T_LOGICAL, LGLSXP},
    {FM_OP_GT, FM_T_REAL, FM_T_INTEGER, FM_T_LOGICAL, LGLSXP},
    {FM_OP_GE, FM_T_REAL, FM_T_INTEGER, FM_T_LOGICAL, LGLSXP},

    // logical-logical operators
    {FM_OP_AND, FM_T_LOGICAL, FM_T_LOGICAL, FM_T_LOGICAL, LGLSXP},
    {FM_OP_OR,  FM_T_LOGICAL, FM_T_LOGICAL, FM_T_LOGICAL, LGLSXP},
    {FM_OP_EQ,  FM_T_LOGICAL, FM_T_LOGICAL, FM_T_LOGICAL, LGLSXP},
    {FM_OP_NE,  FM_T_LOGICAL, FM_T_LOGICAL, FM_T_LOGICAL, LGLSXP},

    // unary
    {FM_OP_POS, FM_T_INTEGER, FM_T_UNSUPPORTED, FM_T_INTEGER, INTSXP},
    {FM_OP_NEG, FM_T_INTEGER, FM_T_UNSUPPORTED, FM_T_INTEGER, INTSXP},
    {FM_OP_POS, FM_T_REAL, FM_T_UNSUPPORTED, FM_T_REAL, REALSXP},
    {FM_OP_NEG, FM_T_REAL, FM_T_UNSUPPORTED, FM_T_REAL, REALSXP},
    {FM_OP_NOT, FM_T_LOGICAL, FM_T_UNSUPPORTED, FM_T_LOGICAL, LGLSXP},
};

static const fm_type_rule *find_type_rule(fm_op_id op, fm_type_id lhs, fm_type_id rhs)
{
    for (const auto &rule : FM_TYPE_RULES) {
        if (rule.op == op && rule.lhs == lhs && rule.rhs == rhs)
            return &rule;
    }
    return nullptr;
}

//==============================================================================
// Parse operator string
//==============================================================================

static fm_op_id parse_operator(const char *s)
{
    if (!s) return FM_OP_UNKNOWN;
    if (strcmp(s, "+") == 0) return FM_OP_ADD;
    if (strcmp(s, "-") == 0) return FM_OP_SUB;
    if (strcmp(s, "*") == 0) return FM_OP_MUL;
    if (strcmp(s, "/") == 0) return FM_OP_DIV;
    if (strcmp(s, "^") == 0) return FM_OP_POW;
    if (strcmp(s, "%%") == 0) return FM_OP_MOD;
    if (strcmp(s, "%/%") == 0) return FM_OP_IDIV;
    if (strcmp(s, "==") == 0) return FM_OP_EQ;
    if (strcmp(s, "!=") == 0) return FM_OP_NE;
    if (strcmp(s, "<") == 0) return FM_OP_LT;
    if (strcmp(s, "<=") == 0) return FM_OP_LE;
    if (strcmp(s, ">") == 0) return FM_OP_GT;
    if (strcmp(s, ">=") == 0) return FM_OP_GE;
    if (strcmp(s, "&") == 0) return FM_OP_AND;
    if (strcmp(s, "|") == 0) return FM_OP_OR;
    if (strcmp(s, "!") == 0) return FM_OP_NOT;
    return FM_OP_UNKNOWN;
}

//==============================================================================
// Map SEXPTYPE to fm_type_id
//==============================================================================

static fm_type_id sexptype_to_fm_type(SEXPTYPE type)
{
    switch (type) {
    case LGLSXP: return FM_T_LOGICAL;
    case INTSXP: return FM_T_INTEGER;
    case REALSXP: return FM_T_REAL;
    case CPLXSXP: return FM_T_COMPLEX;
    case RAWSXP: return FM_T_RAW;
    case STRSXP: return FM_T_STRING;
    case VECSXP: return FM_T_LIST;
    default: return FM_T_UNSUPPORTED;
    }
}

//==============================================================================
// Build fm_source from SEXP
//==============================================================================

static fm_source build_fm_source(SEXP x)
{
    fm_source src;
    memset(&src, 0, sizeof(src));
    src.sexp_keepalive = x;

    fm_vector *vec = maybe_vector_from_altrep(x);
    if (vec) {
        src.type = sexptype_to_fm_type(vec->type);
        src.data = vec->data;
        src.len = vec->len;
        src.elt_size = element_size(vec->type);
        src.runtime = vec->runtime;
        src.vector = vec;
        src.is_fmalloc = true;
        src.is_scalar = vec->len == 1;
        return src;
    }

    src.is_fmalloc = false;
    src.type = sexptype_to_fm_type(TYPEOF(x));
    src.len = XLENGTH(x);
    src.is_scalar = src.len == 1;

    switch (TYPEOF(x)) {
    case LGLSXP: src.data = LOGICAL(x); src.elt_size = sizeof(int); break;
    case INTSXP: src.data = INTEGER(x); src.elt_size = sizeof(int); break;
    case REALSXP: src.data = REAL(x); src.elt_size = sizeof(double); break;
    case CPLXSXP: src.data = COMPLEX(x); src.elt_size = sizeof(Rcomplex); break;
    case RAWSXP: src.data = RAW(x); src.elt_size = sizeof(Rbyte); break;
    default:
        src.data = nullptr;
        src.elt_size = 0;
        break;
    }

    return src;
}

//==============================================================================
// Choose output runtime
//==============================================================================

static fm_runtime *choose_out_runtime(const fm_source &lhs, const fm_source &rhs, bool unary)
{
    if (unary) { if (lhs.is_fmalloc && lhs.runtime) return lhs.runtime; return nullptr; }
    if (lhs.is_fmalloc && lhs.runtime) return lhs.runtime;
    if (rhs.is_fmalloc && rhs.runtime) return rhs.runtime;
    return nullptr;
}

static void compute_recycling(const fm_source &lhs, const fm_source &rhs,
                              R_xlen_t *out_len, bool *recycle_lhs, bool *recycle_rhs)
{
    R_xlen_t n1 = lhs.len, n2 = rhs.len;
    if (n1 == n2) { *out_len = n1; *recycle_lhs = false; *recycle_rhs = false; return; }
    if (n1 == 0 || n2 == 0) { *out_len = 0; *recycle_lhs = false; *recycle_rhs = false; return; }
    *out_len = (n1 > n2) ? n1 : n2;
    *recycle_lhs = n1 < *out_len;
    *recycle_rhs = n2 < *out_len;
}

static void resolve_shape(SEXP e1, SEXP e2, bool unary, SEXP *out_dim, SEXP *out_dimnames)
{
    *out_dim = R_NilValue; *out_dimnames = R_NilValue;
    SEXP dim1 = Rf_getAttrib(e1, R_DimSymbol);
    SEXP dim2 = unary ? R_NilValue : Rf_getAttrib(e2, R_DimSymbol);

    if (dim1 != R_NilValue && dim2 != R_NilValue) {
        bool conform = true;
        if (XLENGTH(dim1) != XLENGTH(dim2)) {
            conform = false;
        } else {
            for (R_xlen_t i = 0; i < XLENGTH(dim1); i++)
                if (INTEGER(dim1)[i] != INTEGER(dim2)[i])
                    { conform = false; break; }
        }
        if (!conform) Rf_error("non-conformable arrays");
        *out_dim = dim1;
        SEXP dn = Rf_getAttrib(e1, R_DimNamesSymbol);
        if (dn != R_NilValue) *out_dimnames = dn;
    } else if (dim1 != R_NilValue) {
        *out_dim = dim1;
        SEXP dn = Rf_getAttrib(e1, R_DimNamesSymbol);
        if (dn != R_NilValue) *out_dimnames = dn;
    } else if (dim2 != R_NilValue) {
        *out_dim = dim2;
        SEXP dn = Rf_getAttrib(e2, R_DimNamesSymbol);
        if (dn != R_NilValue) *out_dimnames = dn;
    }
}

//==============================================================================
// Build execution plan
//==============================================================================

static bool build_op_plan(SEXP op_name, SEXP e1, SEXP e2, bool unary, fm_op_plan *plan)
{
    memset(plan, 0, sizeof(*plan));
    const char *op_str = CHAR(STRING_ELT(op_name, 0));
    plan->op = parse_operator(op_str);
    if (plan->op == FM_OP_UNKNOWN) return false;
    plan->unary = unary;
    // R uses same operator string for unary and binary; disambiguate
    if (unary) {
        if (plan->op == FM_OP_SUB) plan->op = FM_OP_NEG;
        if (plan->op == FM_OP_ADD) plan->op = FM_OP_POS;
    }

    plan->lhs = build_fm_source(e1);
    if (!unary) plan->rhs = build_fm_source(e2);

    fm_type_id lhs_type = plan->lhs.type;
    fm_type_id rhs_type = unary ? FM_T_UNSUPPORTED : plan->rhs.type;
    const fm_type_rule *rule = find_type_rule(plan->op, lhs_type, rhs_type);
    if (!rule) return false;

    plan->out_type = (fm_type_id)rule->out;
    plan->out_sexptype = (SEXPTYPE)rule->out_sexptype;

    if (unary) {
        plan->out_len = plan->lhs.len;
        plan->recycle_lhs = plan->recycle_rhs = false;
    } else {
        compute_recycling(plan->lhs, plan->rhs, &plan->out_len, &plan->recycle_lhs, &plan->recycle_rhs);
        if (plan->recycle_lhs || plan->recycle_rhs) {
            R_xlen_t min_len = std::min(plan->lhs.len, plan->rhs.len);
            R_xlen_t max_len = std::max(plan->lhs.len, plan->rhs.len);
            if (min_len > 0 && max_len % min_len != 0)
                plan->warn_recycling = true;
        }
    }

    plan->out_runtime = choose_out_runtime(plan->lhs, plan->rhs, unary);
    resolve_shape(e1, e2, unary, &plan->out_dim, &plan->out_dimnames);
    return true;
}

//==============================================================================
// Real op real → logical comparisons (writes int* not double*)
//==============================================================================

static void exec_binary_real_to_logical(
    fm_source *lhs, fm_source *rhs, const fm_op_plan *plan,
    R_xlen_t start, R_xlen_t n, int *out, int (*op)(double, double))
{
    R_xlen_t llen = lhs->len, rlen = rhs->len;
    bool rl = plan->recycle_lhs, rr = plan->recycle_rhs;
    double *ld = static_cast<double*>(lhs->data);
    double *rd = static_cast<double*>(rhs->data);
    for (R_xlen_t k = 0; k < n; k++) {
        R_xlen_t i = start + k;
        double lv = lhs->is_scalar ? ld[0] : ld[rl ? i % llen : i];
        double rv = rhs->is_scalar ? rd[0] : rd[rr ? i % rlen : i];
        out[k] = op(lv, rv);
    }
}

//==============================================================================
// Execution functions by output type
//==============================================================================

static void exec_binary_out_int(
    fm_source *lhs, fm_source *rhs, const fm_op_plan *plan,
    R_xlen_t start, R_xlen_t n, int *out, int (*op)(int, int))
{
    R_xlen_t llen = lhs->len, rlen = rhs->len;
    bool rl = plan->recycle_lhs, rr = plan->recycle_rhs;
    int *ld = static_cast<int*>(lhs->data);
    int *rd = static_cast<int*>(rhs->data);

    if (ld && rd && !rl && !rr && llen == rlen && llen >= start + n) {
        for (R_xlen_t i = 0; i < n; i++) out[i] = op(ld[start + i], rd[start + i]);
        return;
    }
    for (R_xlen_t k = 0; k < n; k++) {
        R_xlen_t i = start + k;
        int lv = lhs->is_scalar ? ld[0] : ld[rl ? i % llen : i];
        int rv = rhs->is_scalar ? rd[0] : rd[rr ? i % rlen : i];
        out[k] = op(lv, rv);
    }
}

static void exec_binary_out_int_mixed(
    fm_source *lhs, fm_source *rhs, const fm_op_plan *plan,
    R_xlen_t start, R_xlen_t n, int *out, int (*op)(int, double))
{
    R_xlen_t llen = lhs->len, rlen = rhs->len;
    bool rl = plan->recycle_lhs, rr = plan->recycle_rhs;
    int *ld = static_cast<int*>(lhs->data);
    double *rd = static_cast<double*>(rhs->data);
    for (R_xlen_t k = 0; k < n; k++) {
        R_xlen_t i = start + k;
        int lv = lhs->is_scalar ? ld[0] : ld[rl ? i % llen : i];
        double rv = rhs->is_scalar ? rd[0] : rd[rr ? i % rlen : i];
        out[k] = op(lv, rv);
    }
}

static void exec_binary_out_int_mixed2(
    fm_source *lhs, fm_source *rhs, const fm_op_plan *plan,
    R_xlen_t start, R_xlen_t n, int *out, int (*op)(double, int))
{
    R_xlen_t llen = lhs->len, rlen = rhs->len;
    bool rl = plan->recycle_lhs, rr = plan->recycle_rhs;
    double *ld = static_cast<double*>(lhs->data);
    int *rd = static_cast<int*>(rhs->data);
    for (R_xlen_t k = 0; k < n; k++) {
        R_xlen_t i = start + k;
        double lv = lhs->is_scalar ? ld[0] : ld[rl ? i % llen : i];
        int rv = rhs->is_scalar ? rd[0] : rd[rr ? i % rlen : i];
        out[k] = op(lv, rv);
    }
}

static void exec_binary_out_double(
    fm_source *lhs, fm_source *rhs, const fm_op_plan *plan,
    R_xlen_t start, R_xlen_t n, double *out, double (*op)(double, double))
{
    R_xlen_t llen = lhs->len, rlen = rhs->len;
    bool rl = plan->recycle_lhs, rr = plan->recycle_rhs;
    double *ld = static_cast<double*>(lhs->data);
    double *rd = static_cast<double*>(rhs->data);

    if (ld && rd && !rl && !rr && llen == rlen && llen >= start + n) {
        for (R_xlen_t i = 0; i < n; i++) out[i] = op(ld[start + i], rd[start + i]);
        return;
    }
    for (R_xlen_t k = 0; k < n; k++) {
        R_xlen_t i = start + k;
        double lv = lhs->is_scalar ? ld[0] : ld[rl ? i % llen : i];
        double rv = rhs->is_scalar ? rd[0] : rd[rr ? i % rlen : i];
        out[k] = op(lv, rv);
    }
}

static void exec_binary_out_double_int_int(
    fm_source *lhs, fm_source *rhs, const fm_op_plan *plan,
    R_xlen_t start, R_xlen_t n, double *out, double (*op)(int, int))
{
    R_xlen_t llen = lhs->len, rlen = rhs->len;
    bool rl = plan->recycle_lhs, rr = plan->recycle_rhs;
    int *ld = static_cast<int*>(lhs->data);
    int *rd = static_cast<int*>(rhs->data);
    for (R_xlen_t k = 0; k < n; k++) {
        R_xlen_t i = start + k;
        int lv = lhs->is_scalar ? ld[0] : ld[rl ? i % llen : i];
        int rv = rhs->is_scalar ? rd[0] : rd[rr ? i % rlen : i];
        out[k] = op(lv, rv);
    }
}

static void exec_binary_out_double_mixed(
    fm_source *lhs, fm_source *rhs, const fm_op_plan *plan,
    R_xlen_t start, R_xlen_t n, double *out, double (*op)(int, double))
{
    R_xlen_t llen = lhs->len, rlen = rhs->len;
    bool rl = plan->recycle_lhs, rr = plan->recycle_rhs;
    int *ld = static_cast<int*>(lhs->data);
    double *rd = static_cast<double*>(rhs->data);
    for (R_xlen_t k = 0; k < n; k++) {
        R_xlen_t i = start + k;
        int lv = lhs->is_scalar ? ld[0] : ld[rl ? i % llen : i];
        double rv = rhs->is_scalar ? rd[0] : rd[rr ? i % rlen : i];
        out[k] = op(lv, rv);
    }
}

static void exec_binary_out_double_mixed2(
    fm_source *lhs, fm_source *rhs, const fm_op_plan *plan,
    R_xlen_t start, R_xlen_t n, double *out, double (*op)(double, int))
{
    R_xlen_t llen = lhs->len, rlen = rhs->len;
    bool rl = plan->recycle_lhs, rr = plan->recycle_rhs;
    double *ld = static_cast<double*>(lhs->data);
    int *rd = static_cast<int*>(rhs->data);
    for (R_xlen_t k = 0; k < n; k++) {
        R_xlen_t i = start + k;
        double lv = lhs->is_scalar ? ld[0] : ld[rl ? i % llen : i];
        int rv = rhs->is_scalar ? rd[0] : rd[rr ? i % rlen : i];
        out[k] = op(lv, rv);
    }
}

static void exec_unary_out_int(
    fm_source *src, const fm_op_plan *plan,
    R_xlen_t start, R_xlen_t n, int *out, int (*op)(int))
{
    (void)plan;
    int *d = static_cast<int*>(src->data);
    R_xlen_t len = src->len;
    for (R_xlen_t k = 0; k < n; k++) {
        R_xlen_t i = start + k;
        out[k] = op(d[i >= len ? len - 1 : i]);
    }
}

static void exec_unary_out_double(
    fm_source *src, const fm_op_plan *plan,
    R_xlen_t start, R_xlen_t n, double *out, double (*op)(double))
{
    (void)plan;
    double *d = static_cast<double*>(src->data);
    R_xlen_t len = src->len;
    for (R_xlen_t k = 0; k < n; k++) {
        R_xlen_t i = start + k;
        out[k] = op(d[i >= len ? len - 1 : i]);
    }
}

//==============================================================================
// Main execution planner
//==============================================================================

static SEXP make_result(fm_op_plan *plan, SEXP ans)
{
    if (plan->out_dim != R_NilValue) {
        Rf_setAttrib(ans, R_DimSymbol, plan->out_dim);
        if (plan->out_dimnames != R_NilValue)
            Rf_setAttrib(ans, R_DimNamesSymbol, plan->out_dimnames);
    }
    return ans;
}

static SEXP execute_plan(fm_op_plan *plan)
{
    if (!plan->out_runtime)
        Rf_error("fmalloc Ops requires an fmalloc runtime for the output");

    if (plan->out_len == 0) {
        fm_vector *zvec = allocate_fm_vector(plan->out_runtime, plan->out_sexptype, 0, true, false);
        SEXP ans = PROTECT(fmalloc_new_altrep(zvec));
        ans = make_result(plan, ans);
        UNPROTECT(1);
        return ans;
    }

    // Handle real→logical and mixed→logical comparisons separately (they need int* output)
    if (!plan->unary && plan->out_sexptype == LGLSXP) {
        fm_vector *out_vec = allocate_fm_vector(plan->out_runtime, LGLSXP, plan->out_len, true, false);
        SEXP ans = PROTECT(fmalloc_new_altrep(out_vec));
        int *out_ptr = static_cast<int*>(vector_data_or_dummy(out_vec));

        if (plan->warn_recycling)
            Rf_warning("longer object length is not a multiple of shorter object length");

        bool lr = plan->lhs.type == FM_T_REAL || plan->lhs.type == FM_T_LOGICAL;
        bool rr = plan->rhs.type == FM_T_REAL || plan->rhs.type == FM_T_LOGICAL;
        bool li = plan->lhs.type == FM_T_INTEGER;
        bool ri = plan->rhs.type == FM_T_INTEGER;

        const R_xlen_t CHUNK = 65536;

        if (li && ri) {
            // int op int → logical
            int (*op_fn)(int, int) = nullptr;
            switch (plan->op) {
            case FM_OP_EQ: op_fn = e_eq_ii; break;
            case FM_OP_NE: op_fn = e_ne_ii; break;
            case FM_OP_LT: op_fn = e_lt_ii; break;
            case FM_OP_LE: op_fn = e_le_ii; break;
            case FM_OP_GT: op_fn = e_gt_ii; break;
            case FM_OP_GE: op_fn = e_ge_ii; break;
            default: break;
            }
            if (op_fn) {
                for (R_xlen_t s = 0; s < plan->out_len; s += CHUNK) {
                    R_xlen_t cn = std::min(CHUNK, plan->out_len - s);
                    exec_binary_out_int(&plan->lhs, &plan->rhs, plan, s, cn, out_ptr + s, op_fn);
                    if ((s & 0xFFFFF) == 0) R_CheckUserInterrupt();
                }
            }
        } else if (li && rr) {
            int (*op_fn)(int, double) = nullptr;
            switch (plan->op) {
            case FM_OP_EQ: op_fn = e_eq_id; break;
            case FM_OP_NE: op_fn = e_ne_id; break;
            case FM_OP_LT: op_fn = e_lt_id; break;
            case FM_OP_LE: op_fn = e_le_id; break;
            case FM_OP_GT: op_fn = e_gt_id; break;
            case FM_OP_GE: op_fn = e_ge_id; break;
            default: break;
            }
            if (op_fn) {
                for (R_xlen_t s = 0; s < plan->out_len; s += CHUNK) {
                    R_xlen_t cn = std::min(CHUNK, plan->out_len - s);
                    exec_binary_out_int_mixed(&plan->lhs, &plan->rhs, plan, s, cn, out_ptr + s, op_fn);
                    if ((s & 0xFFFFF) == 0) R_CheckUserInterrupt();
                }
            }
        } else if (lr && ri) {
            int (*op_fn)(double, int) = nullptr;
            switch (plan->op) {
            case FM_OP_EQ: op_fn = e_eq_di; break;
            case FM_OP_NE: op_fn = e_ne_di; break;
            case FM_OP_LT: op_fn = e_lt_di; break;
            case FM_OP_LE: op_fn = e_le_di; break;
            case FM_OP_GT: op_fn = e_gt_di; break;
            case FM_OP_GE: op_fn = e_ge_di; break;
            default: break;
            }
            if (op_fn) {
                for (R_xlen_t s = 0; s < plan->out_len; s += CHUNK) {
                    R_xlen_t cn = std::min(CHUNK, plan->out_len - s);
                    exec_binary_out_int_mixed2(&plan->lhs, &plan->rhs, plan, s, cn, out_ptr + s, op_fn);
                    if ((s & 0xFFFFF) == 0) R_CheckUserInterrupt();
                }
            }
        } else if (lr && rr) {
            int (*op_fn)(double, double) = nullptr;
            switch (plan->op) {
            case FM_OP_EQ: op_fn = e_eq_dd; break;
            case FM_OP_NE: op_fn = e_ne_dd; break;
            case FM_OP_LT: op_fn = e_lt_dd; break;
            case FM_OP_LE: op_fn = e_le_dd; break;
            case FM_OP_GT: op_fn = e_gt_dd; break;
            case FM_OP_GE: op_fn = e_ge_dd; break;
            default: break;
            }
            if (op_fn) {
                for (R_xlen_t s = 0; s < plan->out_len; s += CHUNK) {
                    R_xlen_t cn = std::min(CHUNK, plan->out_len - s);
                    exec_binary_real_to_logical(&plan->lhs, &plan->rhs, plan, s, cn, out_ptr + s, op_fn);
                    if ((s & 0xFFFFF) == 0) R_CheckUserInterrupt();
                }
            }
        }

        // Logical-logical AND/OR
        if (plan->lhs.type == FM_T_LOGICAL && plan->rhs.type == FM_T_LOGICAL) {
            int (*op_fn)(int, int) = nullptr;
            switch (plan->op) {
            case FM_OP_AND: op_fn = e_ll_and; break;
            case FM_OP_OR:  op_fn = e_ll_or; break;
            case FM_OP_EQ:  op_fn = e_ll_eq; break;
            case FM_OP_NE:  op_fn = e_ll_ne; break;
            default: break;
            }
            if (op_fn) {
                for (R_xlen_t s = 0; s < plan->out_len; s += CHUNK) {
                    R_xlen_t cn = std::min(CHUNK, plan->out_len - s);
                    exec_binary_out_int(&plan->lhs, &plan->rhs, plan, s, cn, out_ptr + s, op_fn);
                    if ((s & 0xFFFFF) == 0) R_CheckUserInterrupt();
                }
            }
        }

        ans = make_result(plan, ans);
        UNPROTECT(1);
        return ans;
    }

    // Unary logical NOT
    if (plan->unary && plan->out_sexptype == LGLSXP && plan->op == FM_OP_NOT) {
        fm_vector *out_vec = allocate_fm_vector(plan->out_runtime, LGLSXP, plan->out_len, true, false);
        SEXP ans = PROTECT(fmalloc_new_altrep(out_vec));
        int *out_ptr = static_cast<int*>(vector_data_or_dummy(out_vec));
        const R_xlen_t CHUNK = 65536;
        for (R_xlen_t s = 0; s < plan->out_len; s += CHUNK) {
            R_xlen_t cn = std::min(CHUNK, plan->out_len - s);
            exec_unary_out_int(&plan->lhs, plan, s, cn, out_ptr + s, e_not_l);
            if ((s & 0xFFFFF) == 0) R_CheckUserInterrupt();
        }
        ans = make_result(plan, ans);
        UNPROTECT(1);
        return ans;
    }

    // General case: allocate output and run kernels
    fm_vector *out_vec = allocate_fm_vector(plan->out_runtime, plan->out_sexptype, plan->out_len, true, false);
    SEXP ans = PROTECT(fmalloc_new_altrep(out_vec));
    void *out_data = vector_data_or_dummy(out_vec);
    const R_xlen_t CHUNK = 65536;

    if (plan->warn_recycling)
        Rf_warning("longer object length is not a multiple of shorter object length");

    if (!plan->unary) {
        bool li = plan->lhs.type == FM_T_INTEGER;
        bool lr = plan->lhs.type == FM_T_REAL;
        bool ll = plan->lhs.type == FM_T_LOGICAL;
        bool ri = plan->rhs.type == FM_T_INTEGER;
        bool rr = plan->rhs.type == FM_T_REAL;
        bool rl = plan->rhs.type == FM_T_LOGICAL;

        if (plan->out_sexptype == INTSXP) {
            // Only int op int → int here (logical handled above)
            int *ptr = static_cast<int*>(out_data);
            int (*op_fn)(int, int) = nullptr;
            switch (plan->op) {
            case FM_OP_ADD: op_fn = e_add_ii; break;
            case FM_OP_SUB: op_fn = e_sub_ii; break;
            case FM_OP_MUL: op_fn = e_mul_ii; break;
            case FM_OP_IDIV: op_fn = e_idiv_ii; break;
            case FM_OP_MOD: op_fn = e_mod_ii; break;
            default: break;
            }
            if (op_fn && li && ri) {
                for (R_xlen_t s = 0; s < plan->out_len; s += CHUNK) {
                    R_xlen_t cn = std::min(CHUNK, plan->out_len - s);
                    exec_binary_out_int(&plan->lhs, &plan->rhs, plan, s, cn, ptr + s, op_fn);
                    if ((s & 0xFFFFF) == 0) R_CheckUserInterrupt();
                }
            }
        } else if (plan->out_sexptype == REALSXP) {
            double *ptr = static_cast<double*>(out_data);

            if (li && ri) {
                double (*op_fn)(int, int) = nullptr;
                switch (plan->op) {
                case FM_OP_DIV: op_fn = e_div_ii; break;
                case FM_OP_POW: op_fn = nullptr; break;
                default: break;
                }
                if (op_fn) {
                    for (R_xlen_t s = 0; s < plan->out_len; s += CHUNK) {
                        R_xlen_t cn = std::min(CHUNK, plan->out_len - s);
                        exec_binary_out_double_int_int(&plan->lhs, &plan->rhs, plan, s, cn, ptr + s, op_fn);
                        if ((s & 0xFFFFF) == 0) R_CheckUserInterrupt();
                    }
                }
                if (plan->op == FM_OP_POW) {
                    for (R_xlen_t s = 0; s < plan->out_len; s += CHUNK) {
                        R_xlen_t cn = std::min(CHUNK, plan->out_len - s);
                        exec_binary_out_double_int_int(&plan->lhs, &plan->rhs, plan, s, cn, ptr + s, e_pow_ii);
                        if ((s & 0xFFFFF) == 0) R_CheckUserInterrupt();
                    }
                }
            } else if (lr && rr) {
                double (*op_fn)(double, double) = nullptr;
                switch (plan->op) {
                case FM_OP_ADD: op_fn = e_add_dd; break;
                case FM_OP_SUB: op_fn = e_sub_dd; break;
                case FM_OP_MUL: op_fn = e_mul_dd; break;
                case FM_OP_DIV: op_fn = e_div_dd; break;
                case FM_OP_POW: op_fn = e_pow_dd; break;
                case FM_OP_MOD: op_fn = e_mod_dd; break;
                default: break;
                }
                if (op_fn) {
                    for (R_xlen_t s = 0; s < plan->out_len; s += CHUNK) {
                        R_xlen_t cn = std::min(CHUNK, plan->out_len - s);
                        exec_binary_out_double(&plan->lhs, &plan->rhs, plan, s, cn, ptr + s, op_fn);
                        if ((s & 0xFFFFF) == 0) R_CheckUserInterrupt();
                    }
                }
            } else if (li && (rr || rl)) {
                double (*op_fn)(int, double) = nullptr;
                switch (plan->op) {
                case FM_OP_ADD: op_fn = e_add_id; break;
                case FM_OP_SUB: op_fn = e_sub_id; break;
                case FM_OP_MUL: op_fn = e_mul_id; break;
                case FM_OP_DIV: op_fn = e_div_id; break;
                case FM_OP_POW: op_fn = e_pow_id; break;
                default: break;
                }
                if (op_fn) {
                    for (R_xlen_t s = 0; s < plan->out_len; s += CHUNK) {
                        R_xlen_t cn = std::min(CHUNK, plan->out_len - s);
                        exec_binary_out_double_mixed(&plan->lhs, &plan->rhs, plan, s, cn, ptr + s, op_fn);
                        if ((s & 0xFFFFF) == 0) R_CheckUserInterrupt();
                    }
                }
            } else if ((lr || ll) && ri) {
                double (*op_fn)(double, int) = nullptr;
                switch (plan->op) {
                case FM_OP_ADD: op_fn = e_add_di; break;
                case FM_OP_SUB: op_fn = e_sub_di; break;
                case FM_OP_MUL: op_fn = e_mul_di; break;
                case FM_OP_DIV: op_fn = e_div_di; break;
                case FM_OP_POW: op_fn = e_pow_di; break;
                default: break;
                }
                if (op_fn) {
                    for (R_xlen_t s = 0; s < plan->out_len; s += CHUNK) {
                        R_xlen_t cn = std::min(CHUNK, plan->out_len - s);
                        exec_binary_out_double_mixed2(&plan->lhs, &plan->rhs, plan, s, cn, ptr + s, op_fn);
                        if ((s & 0xFFFFF) == 0) R_CheckUserInterrupt();
                    }
                }
            } else if (ll && (rr || rl)) {
                double (*op_fn)(double, double) = nullptr;
                switch (plan->op) {
                case FM_OP_ADD: op_fn = e_add_dd; break;
                case FM_OP_SUB: op_fn = e_sub_dd; break;
                case FM_OP_MUL: op_fn = e_mul_dd; break;
                case FM_OP_DIV: op_fn = e_div_dd; break;
                default: break;
                }
                if (op_fn) {
                    for (R_xlen_t s = 0; s < plan->out_len; s += CHUNK) {
                        R_xlen_t cn = std::min(CHUNK, plan->out_len - s);
                        exec_binary_out_double(&plan->lhs, &plan->rhs, plan, s, cn, ptr + s, op_fn);
                        if ((s & 0xFFFFF) == 0) R_CheckUserInterrupt();
                    }
                }
            } else if ((lr || ll) && rl) {
                double (*op_fn)(double, double) = nullptr;
                switch (plan->op) {
                case FM_OP_ADD: op_fn = e_add_dd; break;
                case FM_OP_SUB: op_fn = e_sub_dd; break;
                case FM_OP_MUL: op_fn = e_mul_dd; break;
                case FM_OP_DIV: op_fn = e_div_dd; break;
                default: break;
                }
                if (op_fn) {
                    for (R_xlen_t s = 0; s < plan->out_len; s += CHUNK) {
                        R_xlen_t cn = std::min(CHUNK, plan->out_len - s);
                        exec_binary_out_double(&plan->lhs, &plan->rhs, plan, s, cn, ptr + s, op_fn);
                        if ((s & 0xFFFFF) == 0) R_CheckUserInterrupt();
                    }
                }
            }
        }
    } else {
        // Unary: INTSXP or REALSXP (LGLSXP handled above)
        if (plan->out_sexptype == INTSXP) {
            int *ptr = static_cast<int*>(out_data);
            int (*op_fn)(int) = nullptr;
            switch (plan->op) {
            case FM_OP_POS: op_fn = e_pos_i; break;
            case FM_OP_NEG: op_fn = e_neg_i; break;
            default: break;
            }
            if (op_fn) {
                for (R_xlen_t s = 0; s < plan->out_len; s += CHUNK) {
                    R_xlen_t cn = std::min(CHUNK, plan->out_len - s);
                    exec_unary_out_int(&plan->lhs, plan, s, cn, ptr + s, op_fn);
                    if ((s & 0xFFFFF) == 0) R_CheckUserInterrupt();
                }
            }
        } else if (plan->out_sexptype == REALSXP) {
            double *ptr = static_cast<double*>(out_data);
            double (*op_fn)(double) = nullptr;
            switch (plan->op) {
            case FM_OP_POS: op_fn = e_pos_d; break;
            case FM_OP_NEG: op_fn = e_neg_d; break;
            default: break;
            }
            if (op_fn) {
                for (R_xlen_t s = 0; s < plan->out_len; s += CHUNK) {
                    R_xlen_t cn = std::min(CHUNK, plan->out_len - s);
                    exec_unary_out_double(&plan->lhs, plan, s, cn, ptr + s, op_fn);
                    if ((s & 0xFFFFF) == 0) R_CheckUserInterrupt();
                }
            }
        }
    }

    ans = make_result(plan, ans);
    UNPROTECT(1);
    return ans;
}

//==============================================================================
// Native matrix algebra kernels for %*%, crossprod, tcrossprod
//==============================================================================

enum fm_linalg_op_id {
    FM_LINALG_MATMUL = 0,
    FM_LINALG_CROSSPROD = 1,
    FM_LINALG_TCROSSPROD = 2
};

struct fm_linalg_operand {
    fm_type_id type;
    void *data;
    fm_runtime *runtime;
    R_xlen_t len;
    R_xlen_t nrow;
    R_xlen_t ncol;
    bool has_dim;
};

struct fm_linalg_plan {
    fm_linalg_operand lhs;
    fm_linalg_operand rhs;
    R_xlen_t out_nrow;
    R_xlen_t out_ncol;
    R_xlen_t inner_dim;
    R_xlen_t out_len;
    fm_runtime *out_runtime;
    SEXPTYPE out_sexptype;
};

static inline bool is_linalg_supported_type(fm_type_id type)
{
    return type == FM_T_LOGICAL || type == FM_T_INTEGER || type == FM_T_REAL ||
           type == FM_T_COMPLEX;
}

static inline bool is_matrix_dim_operand(SEXP dim)
{
    return dim != R_NilValue && TYPEOF(dim) == INTSXP && XLENGTH(dim) == 2;
}

static void extract_matrix_dimnames(SEXP vec, int which, SEXP *value)
{
    SEXP dimnames = Rf_getAttrib(vec, R_DimNamesSymbol);
    if (dimnames == R_NilValue || TYPEOF(dimnames) != VECSXP || XLENGTH(dimnames) <= which) {
        *value = R_NilValue;
        return;
    }
    *value = VECTOR_ELT(dimnames, which);
}

static bool fmalloc_init_linalg_operand(SEXP vec, fm_linalg_operand *operand)
{
    fm_source source = build_fm_source(vec);
    if (!is_linalg_supported_type(source.type)) {
        return false;
    }

    memset(operand, 0, sizeof(*operand));
    operand->type = source.type;
    operand->data = source.data;
    operand->runtime = source.runtime;
    operand->len = source.len;

    SEXP dim = Rf_getAttrib(vec, R_DimSymbol);
    if (is_matrix_dim_operand(dim)) {
        operand->nrow = (R_xlen_t)INTEGER(dim)[0];
        operand->ncol = (R_xlen_t)INTEGER(dim)[1];
        operand->has_dim = true;
    } else {
        operand->nrow = source.len;
        operand->ncol = 1;
        operand->has_dim = false;
    }

    return true;
}

static void build_linalg_op_plan(fm_linalg_op_id op,
                                const fm_linalg_operand &lhs_in,
                                const fm_linalg_operand &rhs_in,
                                fm_linalg_plan *plan)
{
    fm_linalg_operand lhs = lhs_in;
    fm_linalg_operand rhs = rhs_in;

    bool lhs_is_vector = !lhs.has_dim;
    bool rhs_is_vector = !rhs.has_dim;

    if (lhs.nrow < 0 || lhs.ncol < 0 || rhs.nrow < 0 || rhs.ncol < 0) {
        Rf_error("non-conformable arguments");
    }

    if (op == FM_LINALG_MATMUL) {
        if (lhs_is_vector && rhs_is_vector) {
            if (lhs.len != rhs.len) {
                Rf_error("non-conformable arguments");
            }
            lhs.nrow = 1;
            lhs.ncol = lhs.len;
            rhs.nrow = rhs.len;
            rhs.ncol = 1;
        } else if (lhs_is_vector) {
            if (lhs.len == rhs.nrow) {
                lhs.nrow = 1;
                lhs.ncol = lhs.len;
            } else if (rhs.nrow == 1) {
                lhs.nrow = lhs.len;
                lhs.ncol = 1;
            } else {
                Rf_error("non-conformable arguments");
            }
        } else if (rhs_is_vector) {
            if (rhs.len == lhs.ncol) {
                rhs.nrow = rhs.len;
                rhs.ncol = 1;
            } else if (lhs.ncol == 1) {
                rhs.nrow = 1;
                rhs.ncol = rhs.len;
            } else {
                Rf_error("non-conformable arguments");
            }
        } else if (lhs.ncol != rhs.nrow) {
            Rf_error("non-conformable arguments");
        }

        plan->out_nrow = lhs.nrow;
        plan->out_ncol = rhs.ncol;
        plan->inner_dim = lhs.ncol;
    } else if (op == FM_LINALG_CROSSPROD) {
        if (lhs.nrow != rhs.nrow) {
            Rf_error("non-conformable arguments");
        }

        plan->out_nrow = lhs.ncol;
        plan->out_ncol = rhs.ncol;
        plan->inner_dim = lhs.nrow;
    } else if (op == FM_LINALG_TCROSSPROD) {
        if (lhs.ncol != rhs.ncol) {
            Rf_error("non-conformable arguments");
        }

        plan->out_nrow = lhs.nrow;
        plan->out_ncol = rhs.nrow;
        plan->inner_dim = lhs.ncol;
    }

    if (plan->out_nrow > 0 && plan->out_ncol > 0 &&
        plan->out_nrow > (std::numeric_limits<R_xlen_t>::max() / plan->out_ncol)) {
        Rf_error("non-conformable arguments");
    }

    if (plan->out_nrow < 0 || plan->out_ncol < 0) {
        Rf_error("non-conformable arguments");
    }

    if (plan->out_nrow > (R_xlen_t)std::numeric_limits<int>::max() ||
        plan->out_ncol > (R_xlen_t)std::numeric_limits<int>::max()) {
        Rf_error("non-conformable arguments");
    }

    plan->out_len = plan->out_nrow * plan->out_ncol;
    plan->out_sexptype = (lhs_in.type == FM_T_COMPLEX || rhs_in.type == FM_T_COMPLEX) ? CPLXSXP : REALSXP;
    plan->out_runtime = lhs_in.runtime ? lhs_in.runtime : rhs_in.runtime;

    if (!plan->out_runtime) {
        Rf_error("fmalloc matrix operation requires an fmalloc runtime");
    }

    plan->lhs = lhs;
    plan->rhs = rhs;
}

static inline double read_real_operand(const fm_linalg_operand &operand, R_xlen_t row, R_xlen_t col)
{
    R_xlen_t idx = row + col * operand.nrow;
    switch (operand.type) {
    case FM_T_LOGICAL: {
        int value = static_cast<int *>(operand.data)[idx];
        if (value == NA_LOGICAL) {
            return NA_REAL;
        }
        return (double)value;
    }
    case FM_T_INTEGER: {
        int value = static_cast<int *>(operand.data)[idx];
        if (value == NA_INTEGER) {
            return NA_REAL;
        }
        return (double)value;
    }
    case FM_T_REAL:
        return static_cast<double *>(operand.data)[idx];
    case FM_T_COMPLEX:
        Rf_error("invalid real kernel for complex data");
        return NA_REAL;
    default:
        return NA_REAL;
    }
}

static inline bool is_na_complex(const Rcomplex &value)
{
    return ISNA(value.r) || ISNA(value.i);
}

static inline Rcomplex read_complex_operand(const fm_linalg_operand &operand, R_xlen_t row, R_xlen_t col)
{
    R_xlen_t idx = row + col * operand.nrow;

    if (operand.type == FM_T_COMPLEX) {
        return static_cast<Rcomplex *>(operand.data)[idx];
    }

    double real_part;
    switch (operand.type) {
    case FM_T_REAL:
        real_part = static_cast<double *>(operand.data)[idx];
        break;
    case FM_T_INTEGER: {
        int value = static_cast<int *>(operand.data)[idx];
        if (value == NA_INTEGER) {
            return {NA_REAL, NA_REAL};
        }
        real_part = (double)value;
        break;
    }
    case FM_T_LOGICAL: {
        int value = static_cast<int *>(operand.data)[idx];
        if (value == NA_LOGICAL) {
            return {NA_REAL, NA_REAL};
        }
        real_part = (double)value;
        break;
    }
    default:
        return {NA_REAL, NA_REAL};
    }

    if (ISNA(real_part)) {
        return {NA_REAL, NA_REAL};
    }
    return {real_part, 0.0};
}

static inline void set_linalg_op_dimnames(fm_linalg_op_id op, SEXP lhs, SEXP rhs,
                                         SEXP *rownames, SEXP *colnames)
{
    *rownames = R_NilValue;
    *colnames = R_NilValue;

    switch (op) {
    case FM_LINALG_MATMUL:
        extract_matrix_dimnames(lhs, 0, rownames);
        extract_matrix_dimnames(rhs, 1, colnames);
        break;
    case FM_LINALG_CROSSPROD:
        extract_matrix_dimnames(lhs, 1, rownames);
        extract_matrix_dimnames(rhs, 1, colnames);
        break;
    case FM_LINALG_TCROSSPROD:
        extract_matrix_dimnames(lhs, 0, rownames);
        extract_matrix_dimnames(rhs, 0, colnames);
        break;
    default:
        break;
    }
}

static inline void store_matrix_attrs(SEXP ans, R_xlen_t nrow, R_xlen_t ncol,
                                     SEXP rownames, SEXP colnames)
{
    SEXP dim = PROTECT(Rf_allocVector(INTSXP, 2));
    INTEGER(dim)[0] = (int)nrow;
    INTEGER(dim)[1] = (int)ncol;
    Rf_setAttrib(ans, R_DimSymbol, dim);

    if (rownames != R_NilValue || colnames != R_NilValue) {
        SEXP dimnames = PROTECT(Rf_allocVector(VECSXP, 2));
        SET_VECTOR_ELT(dimnames, 0, rownames);
        SET_VECTOR_ELT(dimnames, 1, colnames);
        Rf_setAttrib(ans, R_DimNamesSymbol, dimnames);
        UNPROTECT(1);
    }

    UNPROTECT(1);
}

static bool linalg_range_all_finite(const double *x, R_xlen_t n)
{
    for (R_xlen_t i = 0; i < n; i++) {
        if (!R_FINITE(x[i])) {
            return false;
        }
    }
    return true;
}

/* BLAS dgemm handles only finite double operands: NA/NaN/Inf propagation is
 * BLAS-implementation-defined, so those fall back to the managed loops, the
 * same split base R's default matprod uses. inner_dim == 0 stays on the
 * loops as well (they write the zeros dgemm would need lda >= 1 for). */
static bool linalg_blas_eligible(const fm_linalg_plan &plan)
{
    if (plan.out_sexptype != REALSXP ||
        plan.lhs.type != FM_T_REAL || plan.rhs.type != FM_T_REAL) {
        return false;
    }
    if (plan.inner_dim <= 0 ||
        plan.inner_dim > (R_xlen_t)std::numeric_limits<int>::max()) {
        return false;
    }

    const double *lhs_data = static_cast<const double *>(plan.lhs.data);
    const double *rhs_data = static_cast<const double *>(plan.rhs.data);
    if (!linalg_range_all_finite(lhs_data, plan.lhs.nrow * plan.lhs.ncol)) {
        return false;
    }
    if (rhs_data != lhs_data &&
        !linalg_range_all_finite(rhs_data, plan.rhs.nrow * plan.rhs.ncol)) {
        return false;
    }
    return true;
}

static void run_linalg_blas(fm_linalg_op_id op, const fm_linalg_plan &plan, double *out)
{
    const double one = 1.0, zero = 0.0;
    int m = (int)plan.out_nrow;
    int n = (int)plan.out_ncol;
    int k = (int)plan.inner_dim;
    int lda = (int)plan.lhs.nrow;
    int ldb = (int)plan.rhs.nrow;
    const char *transa = (op == FM_LINALG_CROSSPROD) ? "T" : "N";
    const char *transb = (op == FM_LINALG_TCROSSPROD) ? "T" : "N";

    rfm_gemm(transa, transb, m, n, k, 1.0,
             static_cast<const double *>(plan.lhs.data), lda,
             static_cast<const double *>(plan.rhs.data), ldb,
             0.0, out, m);
}

static inline Rcomplex complex_add(Rcomplex x, Rcomplex y)
{
    return {x.r + y.r, x.i + y.i};
}

static inline Rcomplex complex_mul(Rcomplex x, Rcomplex y)
{
    return {x.r * y.r - x.i * y.i, x.r * y.i + x.i * y.r};
}

static void run_matmul_real(const fm_linalg_plan &plan, double *out)
{
    for (R_xlen_t col = 0; col < plan.out_ncol; col++) {
        for (R_xlen_t row = 0; row < plan.out_nrow; row++) {
            double acc = 0.0;
            for (R_xlen_t k = 0; k < plan.inner_dim; k++) {
                double lhs_value = read_real_operand(plan.lhs, row, k);
                double rhs_value = read_real_operand(plan.rhs, k, col);
                if (ISNA(lhs_value) || ISNA(rhs_value)) {
                    acc = NA_REAL;
                    break;
                }
                acc += lhs_value * rhs_value;
            }
            out[row + col * plan.out_nrow] = acc;
        }
        if ((col & 0xFFFFF) == 0) {
            R_CheckUserInterrupt();
        }
    }
}

static void run_crossprod_real(const fm_linalg_plan &plan, double *out)
{
    for (R_xlen_t col = 0; col < plan.out_ncol; col++) {
        for (R_xlen_t row = 0; row < plan.out_nrow; row++) {
            double acc = 0.0;
            for (R_xlen_t k = 0; k < plan.inner_dim; k++) {
                double lhs_value = read_real_operand(plan.lhs, k, row);
                double rhs_value = read_real_operand(plan.rhs, k, col);
                if (ISNA(lhs_value) || ISNA(rhs_value)) {
                    acc = NA_REAL;
                    break;
                }
                acc += lhs_value * rhs_value;
            }
            out[row + col * plan.out_nrow] = acc;
        }
        if ((col & 0xFFFFF) == 0) {
            R_CheckUserInterrupt();
        }
    }
}

static void run_tcrossprod_real(const fm_linalg_plan &plan, double *out)
{
    for (R_xlen_t col = 0; col < plan.out_ncol; col++) {
        for (R_xlen_t row = 0; row < plan.out_nrow; row++) {
            double acc = 0.0;
            for (R_xlen_t k = 0; k < plan.inner_dim; k++) {
                double lhs_value = read_real_operand(plan.lhs, row, k);
                double rhs_value = read_real_operand(plan.rhs, col, k);
                if (ISNA(lhs_value) || ISNA(rhs_value)) {
                    acc = NA_REAL;
                    break;
                }
                acc += lhs_value * rhs_value;
            }
            out[row + col * plan.out_nrow] = acc;
        }
        if ((col & 0xFFFFF) == 0) {
            R_CheckUserInterrupt();
        }
    }
}

static void run_matmul_complex(const fm_linalg_plan &plan, Rcomplex *out)
{
    for (R_xlen_t col = 0; col < plan.out_ncol; col++) {
        for (R_xlen_t row = 0; row < plan.out_nrow; row++) {
            Rcomplex acc = {0.0, 0.0};
            for (R_xlen_t k = 0; k < plan.inner_dim; k++) {
                Rcomplex lhs_value = read_complex_operand(plan.lhs, row, k);
                Rcomplex rhs_value = read_complex_operand(plan.rhs, k, col);
                if (is_na_complex(lhs_value) || is_na_complex(rhs_value)) {
                    acc = {NA_REAL, NA_REAL};
                    break;
                }
                acc = complex_add(acc, complex_mul(lhs_value, rhs_value));
            }
            out[row + col * plan.out_nrow] = acc;
        }
        if ((col & 0xFFFFF) == 0) {
            R_CheckUserInterrupt();
        }
    }
}

static void run_crossprod_complex(const fm_linalg_plan &plan, Rcomplex *out)
{
    for (R_xlen_t col = 0; col < plan.out_ncol; col++) {
        for (R_xlen_t row = 0; row < plan.out_nrow; row++) {
            Rcomplex acc = {0.0, 0.0};
            for (R_xlen_t k = 0; k < plan.inner_dim; k++) {
                Rcomplex lhs_value = read_complex_operand(plan.lhs, k, row);
                Rcomplex rhs_value = read_complex_operand(plan.rhs, k, col);
                if (is_na_complex(lhs_value) || is_na_complex(rhs_value)) {
                    acc = {NA_REAL, NA_REAL};
                    break;
                }
                acc = complex_add(acc, complex_mul(lhs_value, rhs_value));
            }
            out[row + col * plan.out_nrow] = acc;
        }
        if ((col & 0xFFFFF) == 0) {
            R_CheckUserInterrupt();
        }
    }
}

static void run_tcrossprod_complex(const fm_linalg_plan &plan, Rcomplex *out)
{
    for (R_xlen_t col = 0; col < plan.out_ncol; col++) {
        for (R_xlen_t row = 0; row < plan.out_nrow; row++) {
            Rcomplex acc = {0.0, 0.0};
            for (R_xlen_t k = 0; k < plan.inner_dim; k++) {
                Rcomplex lhs_value = read_complex_operand(plan.lhs, row, k);
                Rcomplex rhs_value = read_complex_operand(plan.rhs, col, k);
                if (is_na_complex(lhs_value) || is_na_complex(rhs_value)) {
                    acc = {NA_REAL, NA_REAL};
                    break;
                }
                acc = complex_add(acc, complex_mul(lhs_value, rhs_value));
            }
            out[row + col * plan.out_nrow] = acc;
        }
        if ((col & 0xFFFFF) == 0) {
            R_CheckUserInterrupt();
        }
    }
}

extern "C" SEXP rfm_matrix_ops_dispatch(SEXP op_code, SEXP lhs, SEXP rhs)
{
    if (TYPEOF(op_code) != INTSXP || LENGTH(op_code) != 1) {
        Rf_error("Invalid matrix operation code");
    }

    int op_value = INTEGER(op_code)[0];
    if (op_value == NA_INTEGER || op_value < 0 || op_value > 2) {
        Rf_error("Invalid matrix operation code");
    }

    fm_linalg_op_id op = static_cast<fm_linalg_op_id>(op_value);

    fm_linalg_operand lhs_in;
    if (!fmalloc_init_linalg_operand(lhs, &lhs_in)) {
        Rf_error("unsupported fmalloc matrix operation type");
    }

    fm_linalg_operand rhs_in;
    bool rhs_is_missing = (rhs == R_NilValue || TYPEOF(rhs) == NILSXP);
    if (rhs_is_missing) {
        if (op != FM_LINALG_CROSSPROD && op != FM_LINALG_TCROSSPROD) {
            Rf_error("non-conformable arguments");
        }
        rhs_in = lhs_in;
    } else if (!fmalloc_init_linalg_operand(rhs, &rhs_in)) {
        Rf_error("unsupported fmalloc matrix operation type");
    }

    fm_linalg_plan plan;
    memset(&plan, 0, sizeof(plan));
    build_linalg_op_plan(op, lhs_in, rhs_in, &plan);

    fm_vector *out_vec = allocate_fm_vector(plan.out_runtime, plan.out_sexptype,
                                           plan.out_len, true, false);
    SEXP ans = PROTECT(fmalloc_new_altrep(out_vec));

    if (plan.out_len > 0) {
        if (plan.out_sexptype == CPLXSXP) {
            Rcomplex *out = static_cast<Rcomplex*>(vector_data_or_dummy(out_vec));
            if (op == FM_LINALG_MATMUL) {
                run_matmul_complex(plan, out);
            } else if (op == FM_LINALG_CROSSPROD) {
                run_crossprod_complex(plan, out);
            } else {
                run_tcrossprod_complex(plan, out);
            }
        } else {
            double *out = static_cast<double*>(vector_data_or_dummy(out_vec));
            if (linalg_blas_eligible(plan)) {
                run_linalg_blas(op, plan, out);
            } else if (op == FM_LINALG_MATMUL) {
                run_matmul_real(plan, out);
            } else if (op == FM_LINALG_CROSSPROD) {
                run_crossprod_real(plan, out);
            } else {
                run_tcrossprod_real(plan, out);
            }
        }
    }

    SEXP rownames = R_NilValue;
    SEXP colnames = R_NilValue;
    set_linalg_op_dimnames(op, lhs, rhs_is_missing ? lhs : rhs, &rownames, &colnames);
    store_matrix_attrs(ans, plan.out_nrow, plan.out_ncol, rownames, colnames);

    UNPROTECT(1);
    return ans;
}

extern "C" SEXP rfm_can_handle_ops_pair(SEXP x, SEXP y, SEXP generic_sexp)
{
    const char *op_str = CHAR(STRING_ELT(generic_sexp, 0));
    fm_op_id op = parse_operator(op_str);
    if (op == FM_OP_UNKNOWN) return Rf_ScalarLogical(FALSE);

    fm_vector *vx = maybe_vector_from_altrep(x);
    fm_vector *vy = (y != R_NilValue && TYPEOF(y) != NILSXP) ? maybe_vector_from_altrep(y) : nullptr;
    if (!vx && !vy) return Rf_ScalarLogical(FALSE);

    fm_type_id tx = vx ? sexptype_to_fm_type(vx->type) : sexptype_to_fm_type(TYPEOF(x));
    fm_type_id ty = FM_T_UNSUPPORTED;
    if (y != R_NilValue && TYPEOF(y) != NILSXP) {
        ty = vy ? sexptype_to_fm_type(vy->type) : sexptype_to_fm_type(TYPEOF(y));
    }

    auto ok = [](fm_type_id t) { return t == FM_T_INTEGER || t == FM_T_REAL || t == FM_T_LOGICAL || t == FM_T_COMPLEX || t == FM_T_UNSUPPORTED; };
    if (!ok(tx) || (ty != FM_T_UNSUPPORTED && !ok(ty))) return Rf_ScalarLogical(FALSE);

    const fm_type_rule *rule = find_type_rule(op, tx, ty);
    if (!rule && ty != FM_T_UNSUPPORTED) rule = find_type_rule(op, ty, tx);
    if (!rule && ty == FM_T_UNSUPPORTED) rule = find_type_rule(op, tx, ty);
    return Rf_ScalarLogical(rule ? TRUE : FALSE);
}

//==============================================================================
// rfm_ops_dispatch — main entry point called from R
//==============================================================================

extern "C" SEXP rfm_ops_dispatch(SEXP op_name, SEXP e1, SEXP e2, SEXP unary_sexp)
{
    if (TYPEOF(op_name) != STRSXP || LENGTH(op_name) != 1)
        Rf_error("Invalid operator name");

    bool unary = LOGICAL(unary_sexp)[0] == 1;

    fm_op_plan plan;
    if (!build_op_plan(op_name, e1, e2, unary, &plan)) {
        Rf_error("Unsupported fmalloc Ops combination for operator '%s'",
                 CHAR(STRING_ELT(op_name, 0)));
    }

    return execute_plan(&plan);
}
