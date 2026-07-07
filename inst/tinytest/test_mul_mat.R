# End-to-end proof that a C caller can create GGML tensors and run
# ggml_mul_mat() through Rggml's *registered* C-callables (not by calling
# the static implementation functions directly): .Call() dispatches into
# rggml_test.c, which resolves every function it uses - Rggml_context_create,
# Rggml_new_tensor, Rggml_backend_cpu_init, Rggml_compute_mul_mat, etc. - via
# the R_GetCCallable() accessors declared in the installed inst/include/Rggml.h,
# exactly as a downstream LinkingTo package would.
#
# GGML mul_mat convention (verified here, not just asserted): reinterpreting
# each input tensor's raw column-major memory directly as an R matrix of dim
# (ne[0], ne[1]) - i.e. loading an R matrix into a ggml tensor with
# ne = dim(matrix), no copy/transpose needed - ggml_mul_mat(ctx, A, B)
# produces a result tensor of dim (ncol(A), ncol(B)) that, read the same
# way, equals crossprod(A, B) == t(A) %*% B.

# --- known, hand-checkable small case -------------------------------------
A <- matrix(c(1, 2,
              3, 4,
              5, 6), nrow = 2, ncol = 3)  # 2 x 3
B <- matrix(c(1, 0,
              0, 1), nrow = 2, ncol = 2)  # 2 x 2, identity

got <- Rggml:::rggml_test_mul_mat(A, B, zero_copy = FALSE)
want <- crossprod(A, B)  # == t(A) %*% B == t(A) here since B is I_2
expect_equal(dim(got), c(3L, 2L))
expect_equal(got, want, tolerance = 1e-6)
expect_equal(got, t(A), tolerance = 1e-6)

# --- non-trivial known case, hand-computed ---------------------------------
A2 <- matrix(c(1, 2, 3, 4), nrow = 2, ncol = 2)  # cols: (1,2), (3,4)
B2 <- matrix(c(5, 6, 7, 8), nrow = 2, ncol = 2)  # cols: (5,6), (7,8)
# crossprod(A2,B2)[i,j] = sum_k A2[k,i]*B2[k,j]
want2 <- matrix(c(
    1*5 + 2*6, 3*5 + 4*6,
    1*7 + 2*8, 3*7 + 4*8
), nrow = 2, ncol = 2)
got2 <- Rggml:::rggml_test_mul_mat(A2, B2, zero_copy = FALSE)
expect_equal(got2, want2, tolerance = 1e-6)
expect_equal(got2, crossprod(A2, B2), tolerance = 1e-6)

# --- larger random case, both allocation paths ------------------------------
set.seed(20260707)
A3 <- matrix(rnorm(37 * 11), nrow = 37, ncol = 11)
B3 <- matrix(rnorm(37 * 5), nrow = 37, ncol = 5)
expected3 <- crossprod(A3, B3)

got3_alloc <- Rggml:::rggml_test_mul_mat(A3, B3, zero_copy = FALSE)
expect_equal(dim(got3_alloc), c(11L, 5L))
# GGML computes in F32; the tolerance reflects float32, not float64, precision.
expect_equal(got3_alloc, expected3, tolerance = 1e-5)

got3_zerocopy <- Rggml:::rggml_test_mul_mat(A3, B3, zero_copy = TRUE)
expect_equal(got3_zerocopy, expected3, tolerance = 1e-5)

# The two code paths (ggml-managed tensor data vs. externally-owned buffers
# wrapped via the Rggml_new_tensor(..., data) zero-copy argument) must agree
# with each other, not just both be close to the R answer.
expect_equal(got3_alloc, got3_zerocopy, tolerance = 1e-6)

# --- a single vector "matrix" (A is k x 1) works too ------------------------
A4 <- matrix(c(1, 2, 3), nrow = 3, ncol = 1)
B4 <- matrix(c(4, 5, 6), nrow = 3, ncol = 1)
got4 <- Rggml:::rggml_test_mul_mat(A4, B4, zero_copy = FALSE)
expect_equal(as.numeric(got4), sum(A4 * B4), tolerance = 1e-6)

# --- mismatched contracted dimension errors cleanly -------------------------
expect_error(Rggml:::rggml_test_mul_mat(matrix(1:6, 2, 3), matrix(1:6, 3, 2)))
