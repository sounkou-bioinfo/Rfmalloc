/*
 * rggml_cblas.c - cblas_sgemm() implemented over R's Fortran BLAS (sgemm_).
 *
 * Compiled with R's include flags (for F77_NAME from <R_ext/RS.h>) but WITHOUT
 * the GGML rggml_compat.h macro redirections. The final Rggml.so links
 * $(BLAS_LIBS) $(FLIBS), so sgemm_ resolves against whatever BLAS R uses.
 *
 * R does not declare single-precision BLAS in <R_ext/BLAS.h> (it is
 * double-centric), so we declare sgemm_ ourselves via F77_NAME. GGML, like
 * llama.cpp, assumes 32-bit BLAS integers (it truncates int64 dims to int
 * before calling), so `int` here matches GGML's own contract.
 */
#include <R_ext/RS.h> /* F77_NAME / F77_CALL */

extern void F77_NAME(sgemm)(const char *transa, const char *transb,
                            const int *m, const int *n, const int *k,
                            const float *alpha, const float *a, const int *lda,
                            const float *b, const int *ldb,
                            const float *beta, float *c, const int *ldc);

#include "cblas.h"

void cblas_sgemm(CBLAS_ORDER Order, CBLAS_TRANSPOSE TransA, CBLAS_TRANSPOSE TransB,
                 int M, int N, int K,
                 float alpha, const float *A, int lda,
                 const float *B, int ldb,
                 float beta, float *C, int ldc)
{
    const char ta = (TransA == CblasNoTrans) ? 'N' : 'T';
    const char tb = (TransB == CblasNoTrans) ? 'N' : 'T';

    if (Order == CblasRowMajor) {
        /*
         * C is M x N row-major == N x M column-major, and
         * (op(A) op(B))^T = op(B)^T op(A)^T. So a row-major
         * cblas_sgemm(A,B) is the column-major Fortran sgemm(B,A) with M/N
         * and the two transpose flags swapped. This is exactly how the
         * netlib reference CBLAS implements the row-major path.
         */
        F77_NAME(sgemm)(&tb, &ta, &N, &M, &K, &alpha, B, &ldb, A, &lda, &beta, C, &ldc);
    } else {
        F77_NAME(sgemm)(&ta, &tb, &M, &N, &K, &alpha, A, &lda, B, &ldb, &beta, C, &ldc);
    }
}
