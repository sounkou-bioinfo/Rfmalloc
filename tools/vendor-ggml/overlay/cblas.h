/*
 * cblas.h - minimal CBLAS shim for Rggml's vendored GGML BLAS backend.
 *
 * GGML's ggml-blas.cpp calls cblas_sgemm() from a C BLAS (CBLAS) interface.
 * R, however, only guarantees the *Fortran* BLAS interface (sgemm_/dgemm_)
 * via whatever BLAS the R build is linked against (reference libRblas,
 * OpenBLAS, MKL, Accelerate, ...) - it does NOT guarantee CBLAS. So instead
 * of depending on a system <cblas.h>, we provide this minimal header plus a
 * cblas_sgemm() implementation (rggml_cblas.c) that forwards to Fortran
 * sgemm_ via R's F77_NAME() convention. Portable across every BLAS R uses.
 *
 * This header is found (via -I.) ahead of any system cblas.h when compiling
 * ggml-blas.cpp. It intentionally exposes only the symbols ggml-blas.cpp uses.
 */
#ifndef RGGML_CBLAS_SHIM_H
#define RGGML_CBLAS_SHIM_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum CBLAS_ORDER     { CblasRowMajor = 101, CblasColMajor = 102 } CBLAS_ORDER;
typedef enum CBLAS_TRANSPOSE { CblasNoTrans = 111, CblasTrans = 112, CblasConjTrans = 113 } CBLAS_TRANSPOSE;

/* Single-precision GEMM, CBLAS signature. Implemented over Fortran sgemm_. */
void cblas_sgemm(CBLAS_ORDER Order, CBLAS_TRANSPOSE TransA, CBLAS_TRANSPOSE TransB,
                 int M, int N, int K,
                 float alpha, const float *A, int lda,
                 const float *B, int ldb,
                 float beta, float *C, int ldc);

#ifdef __cplusplus
}
#endif

#endif /* RGGML_CBLAS_SHIM_H */
