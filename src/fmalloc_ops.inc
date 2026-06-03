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
// rfm_can_handle_ops_pair — for chooseOpsMethod
//==============================================================================

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
