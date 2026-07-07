library(tinytest)
library(Rggml)

# Rggml stages ISA-specific variants of GGML's hot quantized dot kernels
# (q4_K x q8_K to start) compiled by configure with their own ISA flags
# (-mavx2/-mfma on x86, -O3 NEON on aarch64), selected at runtime by a CPUID
# dispatcher. The staged variant is the same source auto-vectorized (no
# -ffast-math), so it must equal GGML's scalar reference to tight float
# tolerance. This is the correctness guarantee for the dispatch.

message("Testing runtime-SIMD-dispatched q4_K dot (staged variant vs scalar)...")

for (nb in c(1L, 2L, 8L, 32L)) {
    r <- Rggml:::rggml_test_q4k_dot(nb)
    dispatched <- r[1L]
    scalar     <- r[2L]
    expect_true(is.finite(dispatched) && is.finite(scalar))
    expect_true(abs(dispatched) > 0)  # non-degenerate dot
    expect_equal(dispatched, scalar, tolerance = 1e-4)
}

message("SIMD dispatch tests completed")
