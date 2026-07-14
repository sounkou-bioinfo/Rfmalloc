#!/usr/bin/env Rscript

# Compare the current lossless ALP path with a real GGUF quantized weight.
#
# Usage:
#   R_LIBS=/path/to/local/library Rscript experiments/alp_gguf_cpu.R \
#       model.gguf [tensor.name]
#
# The selected tensor must be a two-dimensional q4_k weight. The script uses a
# borrowed GGUF view, so setup does not copy that payload into the Rfmalloc
# backing file. It then materializes one dense reference matrix solely to build
# the ALP controls and measure numerical differences.

main <- function() {
    args <- commandArgs(trailingOnly = TRUE)
    if (length(args) < 1L || !file.exists(args[[1L]])) {
        stop("usage: alp_gguf_cpu.R model.gguf [tensor.name]")
    }
    path <- normalizePath(args[[1L]], mustWork = TRUE)

    suppressPackageStartupMessages({
        library(Rllm)
        library(Rgguf)
        library(Rfmalloc)
    })

    ctx <- gguf_open(path)
    table <- gguf_tensors(ctx)
    candidates <- which(table$type == "q4_k" & table$n_dims == 2L)
    if (!length(candidates)) {
        stop("GGUF contains no two-dimensional q4_k tensor")
    }
    name <- if (length(args) >= 2L) {
        args[[2L]]
    } else {
        target <- 2048^2
        chosen <- candidates[[which.min(abs(
            table$n_elements[candidates] - target
        ))]]
        table$name[[chosen]]
    }
    i <- match(name, table$name)
    if (is.na(i) || table$type[[i]] != "q4_k" || table$n_dims[[i]] != 2L) {
        stop("tensor must name a two-dimensional q4_k weight")
    }
    dims <- table$dims[[i]]
    dense_bytes <- prod(as.double(dims)) * 8

    backing <- tempfile(fileext = ".bin")
    size_gb <- max(0.5, (5 * dense_bytes + 256 * 1024^2) / 2^30)
    runtime <- open_fmalloc(backing, size_gb = size_gb, mode = "scratch")
    on.exit({
        cleanup_fmalloc(runtime)
        unlink(backing)
    }, add = TRUE)

    q4 <- gguf_tensor(ctx, name, runtime = runtime, as = "view")
    dense_fm <- fmalloc_tensor_materialize(q4)
    dense <- matrix(as.double(dense_fm[]), dims[[1L]], dims[[2L]])

    started <- proc.time()[[3L]]
    alp <- as_fmalloc_tensor(dense, "alp", runtime = runtime)
    alp_encode <- proc.time()[[3L]] - started

    # A favorable decimal control distinguishes codec potential from the
    # binary-float distribution found in model weights.
    decimal <- round(dense, 3L)
    started <- proc.time()[[3L]]
    alp_decimal <- as_fmalloc_tensor(decimal, "alp", runtime = runtime)
    alp_decimal_encode <- proc.time()[[3L]] - started

    time_call <- function(fun, inner, reps = 7L) {
        invisible(fun())
        elapsed <- numeric(reps)
        for (r in seq_len(reps)) {
            gc(FALSE)
            started <- proc.time()[[3L]]
            for (j in seq_len(inner)) {
                value <- fun()
                invisible(value[[1L]])
            }
            elapsed[[r]] <- (proc.time()[[3L]] - started) / inner
        }
        c(median = median(elapsed), min = min(elapsed), max = max(elapsed))
    }

    storage <- data.frame(
        path = c("dense_f64", "alp_model_values", "alp_decimal_control", "q4_k"),
        bytes = c(
            dense_bytes,
            length(unclass(alp)),
            length(unclass(alp_decimal)),
            table$nbytes[[i]]
        ),
        stringsAsFactors = FALSE
    )
    storage$bits_per_value <- 8 * storage$bytes / prod(as.double(dims))

    run_batch <- function(batch, inner) {
        set.seed(100L + batch)
        activation <- matrix(rnorm(batch * dims[[1L]]), batch, dims[[1L]])

        fmalloc_matmul_backend("blas")
        dense_time <- time_call(function() activation %*% dense, inner)
        alp_time <- time_call(function() activation %*% alp, inner)
        decimal_time <- time_call(function() activation %*% alp_decimal, inner)
        q4_blas_time <- time_call(function() activation %*% q4, inner)

        fmalloc_matmul_backend("ggml")
        q4_ggml_time <- time_call(function() activation %*% q4, inner)
        q4_value <- activation %*% q4
        reference <- activation %*% dense
        relative_error <- max(abs(q4_value[] - reference)) / max(abs(reference))

        paths <- c(
            "dense_blas", "alp_model_blas", "alp_decimal_blas",
            "q4k_decode_blas", "q4k_native_ggml"
        )
        source_bytes <- c(
            storage$bytes[[1L]], storage$bytes[[2L]], storage$bytes[[3L]],
            storage$bytes[[4L]], storage$bytes[[4L]]
        )
        times <- rbind(
            dense_time, alp_time, decimal_time, q4_blas_time, q4_ggml_time
        )
        data.frame(
            batch = batch,
            path = paths,
            median_ms = 1000 * times[, "median"],
            min_ms = 1000 * times[, "min"],
            effective_source_gbps = source_bytes / times[, "median"] / 1e9,
            q4_relative_error = relative_error,
            stringsAsFactors = FALSE
        )
    }

    timings <- rbind(run_batch(1L, 20L), run_batch(32L, 5L))
    fmalloc_matmul_backend("ggml")

    metadata <- list(
        model = basename(path),
        tensor = name,
        dims = dims,
        blas = extSoftVersion()[["BLAS"]],
        logical_cores = parallel::detectCores(),
        alp_encode_seconds = alp_encode,
        alp_decimal_encode_seconds = alp_decimal_encode
    )
    print(metadata)
    print(storage, row.names = FALSE)
    print(timings, row.names = FALSE)

    out <- Sys.getenv("RFMALLOC_BENCH_OUT", "")
    if (nzchar(out)) {
        write.csv(timings, out, row.names = FALSE)
    }
    invisible(list(metadata = metadata, storage = storage, timings = timings))
}

main()
