#ifndef RFMALLOC_OPS_H
#define RFMALLOC_OPS_H

#ifdef __cplusplus
extern "C" {
#endif

enum fm_op_id {
    FM_OP_ADD = 0,
    FM_OP_SUB,
    FM_OP_MUL,
    FM_OP_DIV,
    FM_OP_POW,
    FM_OP_MOD,
    FM_OP_IDIV,
    FM_OP_EQ,
    FM_OP_NE,
    FM_OP_LT,
    FM_OP_LE,
    FM_OP_GT,
    FM_OP_GE,
    FM_OP_AND,
    FM_OP_OR,
    FM_OP_NOT,
    FM_OP_POS,
    FM_OP_NEG,
    FM_OP_UNKNOWN
};

enum fm_type_id {
    FM_T_LOGICAL = 0,
    FM_T_INTEGER,
    FM_T_REAL,
    FM_T_COMPLEX,
    FM_T_RAW,
    FM_T_STRING,
    FM_T_LIST,
    FM_T_UNSUPPORTED
};

#ifdef __cplusplus
}
#endif

#endif
