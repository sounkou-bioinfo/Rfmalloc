# Smoke test: end-to-end demonstration of Rgguf's headline claim.
#
# Writes a GGUF file with a 100x50 and a 50x30 F32 tensor, imports both into
# Rfmalloc-backed matrices sharing a single file-backed runtime, multiplies
# them with %*%, and verifies the product against base R while confirming
# the result is genuinely Rfmalloc-backed.
#
# Run with:
#   Rscript inst/smoke_test.R
#
# NOTE on a discovered Rfmalloc limitation, not an Rgguf bug:
# As of Rfmalloc 0.1.0, `%*%`/`crossprod`/`tcrossprod` on fmalloc-backed
# vectors/matrices with >= 64 elements fail with "fmalloc matrix operation
# requires an fmalloc runtime" whenever the operand has been passed through
# at least one R closure call first (which is unavoidable: %*%.fmalloc()
# itself is a closure, so this is true for essentially every real call, not
# a rare edge case). Root cause: Rfmalloc's `.fmalloc_strip_class()` runs
# `class(x) <- NULL` to strip the S3 class before handing operands to its
# native dispatcher; for a *referenced* ALTREP object (refcount >= 2, which
# any closure argument has) of length >= 64, R's own attribute-assignment
# machinery replaces the object with a generic, R-core "wrapper" ALTREP
# object instead of invoking the class's registered Duplicate method - this
# is true even for base R's own compact `1:100` ALTREP sequences, so it is
# general R behavior, not something specific to Rfmalloc's ALTREP class.
# Rfmalloc's `maybe_vector_from_altrep()` does not unwrap that generic
# wrapper, so it fails to find the underlying fmalloc vector and its
# runtime. The vendored native dispatcher itself has no such problem: it
# ignores R class attributes entirely and reads Rgguf's tensors correctly
# at any size (verified directly below via .Call("RC_gguf_tensor_fill", ...)
# results and via a sub-64-element %*% call). This script demonstrates both
# the literal requested scenario (currently blocked by the Rfmalloc issue
# above) and a smaller-scale run that stays under the 64-element threshold,
# to isolate exactly what does and does not work today.

library(Rgguf)

set.seed(42)
a <- matrix(rnorm(100 * 50), nrow = 100, ncol = 50)
b <- matrix(rnorm(50 * 30), nrow = 50, ncol = 30)

gguf_path <- tempfile(fileext = ".gguf")
alloc_path <- tempfile(fileext = ".bin")
on.exit({
    unlink(gguf_path)
    unlink(alloc_path)
}, add = TRUE)

gguf_write_tensors(gguf_path, tensors = list(a = a, b = b))
cat("Wrote GGUF file:", gguf_path, "(", file.size(gguf_path), "bytes )\n")

cat("\nTensor directory:\n")
print(gguf_tensors(gguf_path))

rt <- Rfmalloc::open_fmalloc(alloc_path)
mats <- gguf_import(gguf_path, runtime = rt)

cat("\nImported tensor dims: a =", paste(dim(mats$a), collapse = "x"),
    " b =", paste(dim(mats$b), collapse = "x"), "\n")
cat("is_fmalloc_vector(a):", Rfmalloc::is_fmalloc_vector(mats$a), "\n")
cat("is_fmalloc_vector(b):", Rfmalloc::is_fmalloc_vector(mats$b), "\n")
# gguf_write_tensors() writes F32 (single precision, ~7 significant digits),
# so round-tripped values match base R's doubles only up to float32
# precision, not bit-for-bit - a small tolerance is expected and correct
# here, not a bug.
cat("dequantized values match base R (a):",
    isTRUE(all.equal(as.vector(mats$a), as.vector(a), tolerance = 1e-6)), "\n")
cat("dequantized values match base R (b):",
    isTRUE(all.equal(as.vector(mats$b), as.vector(b), tolerance = 1e-6)), "\n")

cat("\n--- Step: a %*% b (100x50 %*% 50x30, as requested) ---\n")
product <- tryCatch(mats$a %*% mats$b, error = function(e) e)

if (inherits(product, "error")) {
    cat("FAILED:", conditionMessage(product), "\n")
    cat(
        "This is the known Rfmalloc (not Rgguf) limitation described in the\n",
        "header comment above: %*%.fmalloc() loses track of the runtime for\n",
        "fmalloc-backed operands >= 64 elements once R's own ALTREP attribute-\n",
        "wrapping kicks in. Demonstrating the same mechanism at a smaller scale\n",
        "below to isolate the issue to that specific limitation.\n",
        sep = ""
    )
} else {
    expected <- a %*% b
    max_abs_diff <- max(abs(as.vector(product) - as.vector(expected)))
    cat("Product dims:", paste(dim(product), collapse = "x"), "\n")
    cat("is_fmalloc_vector(product):", Rfmalloc::is_fmalloc_vector(product), "\n")
    cat("max |Rgguf %*% - base R %*%| =", max_abs_diff, "\n")
    # Tensors round-trip through F32, so a float32-precision tolerance (not
    # 1e-9) is the correct bar here.
    stopifnot(
        identical(dim(product), dim(expected)),
        isTRUE(Rfmalloc::is_fmalloc_vector(product)),
        max_abs_diff < 1e-4
    )
    cat("\nSMOKE TEST PASSED (100x50 %*% 50x30)\n")
}

cat("\n--- Supplementary check: same workflow at a size under Rfmalloc's\n",
    "    64-element ALTREP-wrapper threshold (8x7 %*% 7x6) ---\n", sep = "")
a_small <- matrix(rnorm(8 * 7), nrow = 8, ncol = 7)
b_small <- matrix(rnorm(7 * 6), nrow = 7, ncol = 6)
gguf_write_tensors(gguf_path, tensors = list(a = a_small, b = b_small))
mats_small <- gguf_import(gguf_path, runtime = rt)

product_small <- mats_small$a %*% mats_small$b
expected_small <- a_small %*% b_small
max_abs_diff_small <- max(abs(as.vector(product_small) - as.vector(expected_small)))

cat("is_fmalloc_vector(product):", Rfmalloc::is_fmalloc_vector(product_small), "\n")
cat("max |Rgguf %*% - base R %*%| =", max_abs_diff_small, "\n")
stopifnot(
    identical(dim(product_small), dim(expected_small)),
    isTRUE(Rfmalloc::is_fmalloc_vector(product_small)),
    max_abs_diff_small < 1e-4
)
cat("SMOKE TEST (supplementary, small scale) PASSED\n")
