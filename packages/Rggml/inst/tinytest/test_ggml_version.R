# ggml_version() resolves through the registered "Rggml_version" C-callable
# and returns the vendored GGML library's own runtime version string.

v <- ggml_version()
expect_true(is.character(v))
expect_equal(length(v), 1L)
expect_true(nzchar(v))
