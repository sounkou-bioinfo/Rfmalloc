library(tinytest)
library(Rggml)

if (!isTRUE(rggml_cpu_info()$blas)) {
    exit_file("BLAS backend is not part of this target build")
}

# GGML's BLAS backend (ggml-blas.cpp) offloads the dense F32 mul_mat to
# whatever BLAS the R build links against, reached through our portable
# cblas_sgemm -> Fortran sgemm_ shim. It is a drop-in backend for the same
# registered compute path the CPU backend uses; the result must still equal
# crossprod(A, B). GGML accumulates in F32, so tolerances reflect single, not
# double, precision.

message("Testing GGML BLAS backend (dense F32 mul_mat via R's BLAS)...")

set.seed(42)

# --- BLAS result equals crossprod across shapes and both tensor paths --------
shapes <- list(c(k = 4,  na = 3,  nb = 5),
               c(k = 1,  na = 1,  nb = 1),
               c(k = 16, na = 7,  nb = 9),
               c(k = 64, na = 32, nb = 48))

for (s in shapes) {
    A <- matrix(rnorm(s[["k"]] * s[["na"]]), s[["k"]], s[["na"]])
    B <- matrix(rnorm(s[["k"]] * s[["nb"]]), s[["k"]], s[["nb"]])
    ref <- crossprod(A, B)
    for (zc in c(FALSE, TRUE)) {
        got <- Rggml:::rggml_test_mul_mat(A, B, zero_copy = zc, backend = "blas")
        expect_equal(dim(got), c(s[["na"]], s[["nb"]]))
        expect_equal(got, ref, tolerance = 1e-4)
    }
}

# --- BLAS and CPU backends produce the same product --------------------------
A <- matrix(rnorm(20 * 6), 20, 6)
B <- matrix(rnorm(20 * 4), 20, 4)
cpu  <- Rggml:::rggml_test_mul_mat(A, B, backend = "cpu")
blas <- Rggml:::rggml_test_mul_mat(A, B, backend = "blas")
expect_equal(blas, cpu, tolerance = 1e-4)
expect_equal(blas, crossprod(A, B), tolerance = 1e-4)

message("BLAS backend tests completed")
