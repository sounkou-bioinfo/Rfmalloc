/*
 * rggml_cblas.c - cblas_sgemm() implemented over R's Fortran BLAS.
 *
 * Compiled with R's include flags (for F77_NAME from <R_ext/RS.h>) but WITHOUT
 * the GGML rggml_compat.h macro redirections. The final Rggml.so links
 * $(BLAS_LIBS) $(FLIBS), so the BLAS symbols resolve against whatever BLAS R
 * uses. GGML, like llama.cpp, assumes 32-bit BLAS integers (it truncates int64
 * dims to int before calling), so `int` here matches GGML's own contract.
 *
 * Single precision is NOT guaranteed. R's own BLAS is double-centric:
 * <R_ext/BLAS.h> declares no single-precision routines, and R's Windows
 * Rblas.dll does not export sgemm_ at all (the reference BLAS on most Linux
 * distributions happens to, which is why this went unnoticed until the first
 * Windows build). Only the double routines can be relied on.
 *
 * So ../../configure link-probes for sgemm_ against R's own BLAS_LIBS/FLIBS
 * and defines RGGML_HAVE_SGEMM when it resolves:
 *
 *   RGGML_HAVE_SGEMM   -> forward straight to sgemm_ (no copies)
 *   otherwise          -> promote to double, call dgemm_, demote the result
 *
 * The fallback costs three temporary buffers and two conversions, but keeps a
 * real optimized BLAS underneath - far better than a naive triple loop, and
 * still correct to float precision. RGGML_NO_SGEMM=1 forces it, for testing.
 */
#include <stdlib.h>

#include <R_ext/RS.h> /* F77_NAME */

#ifdef RGGML_HAVE_SGEMM
extern void F77_NAME(sgemm)(const char *transa, const char *transb,
                            const int *m, const int *n, const int *k,
                            const float *alpha, const float *a, const int *lda,
                            const float *b, const int *ldb,
                            const float *beta, float *c, const int *ldc);
#else
extern void F77_NAME(dgemm)(const char *transa, const char *transb,
                            const int *m, const int *n, const int *k,
                            const double *alpha, const double *a, const int *lda,
                            const double *b, const int *ldb,
                            const double *beta, double *c, const int *ldc);
#endif

#include "cblas.h"

#ifndef RGGML_HAVE_SGEMM
/* Last-resort column-major GEMM, used only if the temporaries cannot be
 * allocated. Correct, not fast; reaching it means the process is out of memory
 * anyway. */
static void rggml_gemm_naive(const char ta, const char tb, int m, int n, int k,
                             float alpha, const float *a, int lda,
                             const float *b, int ldb, float beta, float *c, int ldc)
{
    for (int j = 0; j < n; j++) {
        for (int i = 0; i < m; i++) {
            double acc = 0.0;
            for (int p = 0; p < k; p++) {
                const float av = (ta == 'N') ? a[i + (size_t) p * lda] : a[p + (size_t) i * lda];
                const float bv = (tb == 'N') ? b[p + (size_t) j * ldb] : b[j + (size_t) p * ldb];
                acc += (double) av * (double) bv;
            }
            float *dst = &c[i + (size_t) j * ldc];
            *dst = (float) (alpha * (float) acc + beta * (*dst));
        }
    }
}
#endif

/* Column-major (Fortran) sgemm, however we can get it. */
static void rggml_f77_sgemm(const char ta, const char tb, int m, int n, int k,
                            float alpha, const float *a, int lda,
                            const float *b, int ldb, float beta, float *c, int ldc)
{
#ifdef RGGML_HAVE_SGEMM
    F77_NAME(sgemm)(&ta, &tb, &m, &n, &k, &alpha, a, &lda, b, &ldb, &beta, c, &ldc);
#else
    /* Fortran shapes: A is (lda x k) or (lda x m) when transposed; B is
     * (ldb x n) or (ldb x k); C is always (ldc x n). */
    const size_t an = (size_t) lda * (size_t) ((ta == 'N') ? k : m);
    const size_t bn = (size_t) ldb * (size_t) ((tb == 'N') ? n : k);
    const size_t cn = (size_t) ldc * (size_t) n;

    double *ad = (double *) malloc(an * sizeof(double));
    double *bd = (double *) malloc(bn * sizeof(double));
    double *cd = (double *) malloc(cn * sizeof(double));
    if (!ad || !bd || !cd) {
        free(ad); free(bd); free(cd);
        rggml_gemm_naive(ta, tb, m, n, k, alpha, a, lda, b, ldb, beta, c, ldc);
        return;
    }

    for (size_t i = 0; i < an; i++) ad[i] = (double) a[i];
    for (size_t i = 0; i < bn; i++) bd[i] = (double) b[i];
    if (beta != 0.0f) { for (size_t i = 0; i < cn; i++) cd[i] = (double) c[i]; }
    else              { for (size_t i = 0; i < cn; i++) cd[i] = 0.0; }

    const double dalpha = (double) alpha, dbeta = (double) beta;
    F77_NAME(dgemm)(&ta, &tb, &m, &n, &k, &dalpha, ad, &lda, bd, &ldb, &dbeta, cd, &ldc);

    for (size_t i = 0; i < cn; i++) c[i] = (float) cd[i];
    free(ad); free(bd); free(cd);
#endif
}

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
         * (op(A) op(B))^T = op(B)^T op(A)^T. So a row-major cblas_sgemm(A,B) is
         * the column-major Fortran sgemm(B,A) with M/N and the two transpose
         * flags swapped - exactly how the netlib reference CBLAS does it.
         */
        rggml_f77_sgemm(tb, ta, N, M, K, alpha, B, ldb, A, lda, beta, C, ldc);
    } else {
        rggml_f77_sgemm(ta, tb, M, N, K, alpha, A, lda, B, ldb, beta, C, ldc);
    }
}
