# Full-stack integration: run every package's tinytest suite against the
# co-installed stack. This is where the cross-package guarantees actually get
# exercised (codec decoders vs GGML's reference, the typed-GEMM bridge vs the
# decode+BLAS fallback). CI runs this after installing packages/ in dependency
# order (Rfmalloc, Rggml, Rgguf, Rllm); locally, install the same way first.
ok <- TRUE
for (p in c("Rfmalloc", "Rggml", "Rgguf", "Rllm", "Rpgen")) {
    cat("==== ", p, " ====\n", sep = "")
    res <- tinytest::test_package(p)
    print(res)
    ok <- ok && all(vapply(res, isTRUE, logical(1)))
}
if (!ok) stop("integration test failures")
cat("integration: all suites green\n")
