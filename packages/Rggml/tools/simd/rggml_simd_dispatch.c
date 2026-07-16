/*
 * Runtime dispatch for GGML's Q4_K dot product.
 *
 * The portable GGML library is safe on the oldest supported CPU. On x86,
 * configure also compiles GGML's official x86 quant translation unit under
 * private names. This file owns the one canonical symbol currently selected
 * at runtime and enters the staged object only after checking the complete
 * feature set used to compile it.
 */
#include <stddef.h>

#include "cpu_features.h"

extern void ggml_vec_dot_q4_K_q8_K_generic(int, float *, size_t,
    const void *, size_t, const void *, size_t, int);

#ifdef RGGML_HAVE_X86_AVX2
extern void rggml_vec_dot_q4_K_q8_K_avx2(int, float *, size_t,
    const void *, size_t, const void *, size_t, int);
static int rggml_use_avx2;
#endif

void
rggml_simd_dispatch_init(void)
{
#ifdef RGGML_HAVE_X86_AVX2
    rggml_use_avx2 = sd_cpu_has_upstream_avx2();
#endif
}

void
ggml_vec_dot_q4_K_q8_K(int n, float *s, size_t bs, const void *vx,
    size_t bx, const void *vy, size_t by, int nrc)
{
#ifdef RGGML_HAVE_X86_AVX2
    if (rggml_use_avx2) {
        rggml_vec_dot_q4_K_q8_K_avx2(
            n, s, bs, vx, bx, vy, by, nrc);
        return;
    }
#endif
    ggml_vec_dot_q4_K_q8_K_generic(n, s, bs, vx, bx, vy, by, nrc);
}
