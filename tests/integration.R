# Full-stack integration: run every package's tinytest suite against the
# co-installed stack. This is where the cross-package guarantees actually get
# exercised (codec decoders vs GGML's reference, the typed-GEMM bridge vs the
# decode+BLAS fallback). CI runs this after installing packages/ in dependency
# order (Rfmalloc, Rggml, Rgguf, Rllm); locally, install the same way first.

# remotes::remote_package_name() first probes an installed package named after
# the repository. Every sibling lives in the Rfmalloc repository, so an
# unnamed subdirectory remote can be mistaken for the already installed
# Rfmalloc package and skipped solely because the repository SHA matches.
# Require each sibling remote to carry its actual package name.
for (path in Sys.glob("packages/*/DESCRIPTION")) {
    description <- read.dcf(path)
    if (!("Remotes" %in% colnames(description))) {
        next
    }

    remotes <- strsplit(
        description[1L, "Remotes"], ",", fixed = TRUE
    )[[1L]]
    remotes <- trimws(remotes)
    remotes <- remotes[
        grepl("sounkou-bioinfo/Rfmalloc/packages/", remotes, fixed = TRUE)
    ]
    for (remote in remotes) {
        fields <- trimws(strsplit(remote, "=", fixed = TRUE)[[1L]])
        if (length(fields) != 2L) {
            stop("unnamed monorepo remote in ", path, ": ", remote)
        }
        package_name <- basename(sub("@[^@]*$", "", fields[[2L]]))
        if (!identical(fields[[1L]], package_name)) {
            stop("wrong monorepo remote name in ", path, ": ", remote)
        }
    }
}

ok <- TRUE
for (p in c("Rfmalloc", "Rggml", "Rgguf", "Rllm", "Rpgen", "RfmallocStatgen")) {
    cat("==== ", p, " ====\n", sep = "")
    res <- tinytest::test_package(p)
    print(res)
    ok <- ok && all(vapply(res, isTRUE, logical(1)))
}
if (!ok) stop("integration test failures")
cat("integration: all suites green\n")
