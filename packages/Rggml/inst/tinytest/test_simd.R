library(tinytest)
library(Rggml)

# On x86, Rggml stages GGML's official arch/x86 Q4_K dot under its upstream
# ISA flags and selects it through a CPUID dispatcher. Aarch64 and wasm compile
# GGML's complete native architecture sources directly. The staged x86 result
# must agree with GGML's generic reference to tight float tolerance.

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
