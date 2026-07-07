/*
 * rggml_simd_dispatch.c - runtime SIMD dispatch for GGML's hot quantized
 * vec-dot kernels (RsimdDispatch strategy).
 *
 * GGML's type-traits table calls a canonical symbol (e.g.
 * ggml_vec_dot_q4_K_q8_K) for each quantized dot product. Under
 * GGML_CPU_GENERIC that symbol is aliased to the scalar reference. Here we
 * instead *define* that canonical symbol as a dispatcher that, once per
 * process, asks cpu_features which ISA the running CPU supports and forwards
 * to the matching staged variant (compiled by configure with that ISA's
 * flags), falling back to GGML's own scalar reference (..._generic).
 *
 * A variant symbol is only ever *called* after its ISA has been confirmed at
 * runtime, so linking an -mavx2-compiled object is safe on any CPU: it is
 * dead on machines without AVX2. The corresponding alias is removed from
 * ggml-cpu/arch-fallback.h so this file owns the canonical symbol.
 */
#include <stddef.h>

#include "cpu_features.h"

/* GGML's scalar reference implementation (ggml-cpu/quants.c). */
extern void ggml_vec_dot_q4_K_q8_K_generic(int n, float *s, size_t bs,
        const void *vx, size_t bx, const void *vy, size_t by, int nrc);

#ifdef RGGML_HAVE_AVX2
extern void rggml_vec_dot_q4_K_q8_K_avx2(int n, float *s, size_t bs,
        const void *vx, size_t bx, const void *vy, size_t by, int nrc);
#endif
#ifdef RGGML_HAVE_NEON
extern void rggml_vec_dot_q4_K_q8_K_neon(int n, float *s, size_t bs,
        const void *vx, size_t bx, const void *vy, size_t by, int nrc);
#endif

void ggml_vec_dot_q4_K_q8_K(int n, float *s, size_t bs,
        const void *vx, size_t bx, const void *vy, size_t by, int nrc) {
    /* Results are cached across calls; the benign race resolves to the same
       value. On aarch64, NEON is a mandatory baseline so sd_cpu_has_neon()
       is always 1 - no CPUID gate is really needed there, but the uniform
       shape keeps this identical to the (genuinely runtime-gated) x86 path. */
#ifdef RGGML_HAVE_AVX2
    static int use_avx2 = -1;
    if (use_avx2 < 0) {
        use_avx2 = sd_cpu_has_avx2();
    }
    if (use_avx2) {
        rggml_vec_dot_q4_K_q8_K_avx2(n, s, bs, vx, bx, vy, by, nrc);
        return;
    }
#endif
#ifdef RGGML_HAVE_NEON
    static int use_neon = -1;
    if (use_neon < 0) {
        use_neon = sd_cpu_has_neon();
    }
    if (use_neon) {
        rggml_vec_dot_q4_K_q8_K_neon(n, s, bs, vx, bx, vy, by, nrc);
        return;
    }
#endif
    ggml_vec_dot_q4_K_q8_K_generic(n, s, bs, vx, bx, vy, by, nrc);
}
