## rggml_cpu_info(): assert the build took the branch it was supposed to.
##
## Every branch configure can pick is numerically correct, so no other test in
## this suite can tell them apart. A build that silently fell back from GGML's
## native NEON kernels to the portable scalar ones, or from sgemm_ to the dgemm_
## promotion, is green everywhere else. These are the only assertions that fail.

library(Rggml)

info <- rggml_cpu_info()

expect_true(is.list(info))
expect_equal(names(info), c("arch_kernels", "simd_dispatch", "blas", "sgemm",
                            "vulkan", "cuda"))
expect_true(is.character(info$arch_kernels) && length(info$arch_kernels) == 1L)
expect_true(info$arch_kernels %in% c("arm", "wasm", "generic"))
for (f in c("simd_dispatch", "blas", "sgemm", "vulkan", "cuda")) {
    expect_true(is.logical(info[[f]]) && length(info[[f]]) == 1L && !is.na(info[[f]]))
}

## The load-bearing invariant. GGML's arch/arm/quants.c defines the canonical
## ggml_vec_dot_q4_K_q8_K; our staged NEON variant defines the same symbol. If
## both were ever compiled in, the linker picks one and which one is anybody's
## guess. configure must choose exactly one strategy.
expect_false(info$arch_kernels == "arm" && info$simd_dispatch)
expect_false(info$arch_kernels == "wasm" && info$simd_dispatch)
expect_false(info$sgemm && !info$blas)

## aarch64 is where the native kernels are meant to land: NEON is a mandatory
## baseline there, so there is no reason to have fallen back and no runtime gate
## to fall back through. On real ARM CI (ubuntu-24.04-arm, macOS Apple Silicon)
## this is the assertion that proves the vendored arch/arm/quants.c is in the
## binary rather than merely in the tarball.
machine <- unname(Sys.info()[["machine"]])
if (machine %in% c("aarch64", "arm64")) {
    expect_equal(info$arch_kernels, "arm")
    expect_false(info$simd_dispatch)
} else if (machine %in% c("x86_64", "amd64")) {
    ## arch/x86 is deliberately not vendored (its kernels are gated on a
    ## compile-time __AVX2__, so building them would demand AVX2 at runtime), so
    ## x86 gets the portable kernels plus the CPUID dispatcher over them.
    expect_equal(info$arch_kernels, "generic")
    expect_true(info$simd_dispatch)
} else if (grepl("wasm|emscripten", machine, ignore.case = TRUE)) {
    expect_equal(info$arch_kernels, "wasm")
    expect_false(info$simd_dispatch)
    expect_false(info$blas)
}

## Vulkan devices cannot be enumerated by a binary that has no Vulkan backend,
## so a positive device count implies the build flag. Note the implication runs
## one way only: rggml_has_vulkan() reports a visible *device*, and a
## --with-vulkan build on a machine with no driver has the backend and sees
## nothing. info$vulkan is the build fact, rggml_has_vulkan() the runtime one.
if (rggml_has_vulkan()) {
    expect_true(info$vulkan)
}

if (rggml_has_cuda()) {
    expect_true(info$cuda)
}
