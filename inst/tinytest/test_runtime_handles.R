library(tinytest)
library(Rfmalloc)

cat("Testing fmalloc runtime handles and ALTREP lifetime links...\n")

file1 <- tempfile(fileext = ".bin")
file2 <- tempfile(fileext = ".bin")

tryCatch(
    {
        rt1 <- open_fmalloc(file1)
        rt2 <- open_fmalloc(file2, size_gb = 0.1)

        expect_true(inherits(rt1, "fmalloc_runtime"))
        expect_true(inherits(rt2, "fmalloc_runtime"))
        expect_true(isTRUE(attr(rt1, "initialized")))
        expect_true(isTRUE(attr(rt2, "initialized")))

        v1 <- create_fmalloc_vector("integer", 10, runtime = rt1)
        v2 <- create_fmalloc_vector("numeric", 10, runtime = rt2)
        v1[1] <- 11L
        v2[1] <- 22

        cleanup_fmalloc(rt1)
        expect_equal(v1[1], 11L)
        expect_error(
            create_fmalloc_vector("integer", 10, runtime = rt1),
            "runtime is closed"
        )

        v2b <- create_fmalloc_vector("integer", 10, runtime = rt2)
        v2b[1] <- 33L
        expect_equal(v2[1], 22)
        expect_equal(v2b[1], 33L)

        rm(v1)
        gc()

        cleanup_fmalloc(rt2)
        rm(v2, v2b)
        gc()

        cat("  Explicit runtime handle test passed\n")
    },
    finally = {
        existing <- intersect(c("rt1", "rt2", "v1", "v2", "v2b"), ls())
        if (length(existing) > 0) rm(list = existing, inherits = FALSE)
        gc()
        unlink(c(file1, file2))
    }
)

file3 <- tempfile(fileext = ".bin")
tryCatch(
    {
        kept_vector <- local({
            rt <- open_fmalloc(file3)
            x <- create_fmalloc_vector("integer", 10, runtime = rt)
            x[1] <- 44L
            x
        })

        gc()
        expect_equal(kept_vector[1], 44L)

        rm(kept_vector)
        gc()

        cat("  Runtime-drop while vector-live test passed\n")
    },
    finally = {
        existing <- intersect("kept_vector", ls())
        if (length(existing) > 0) rm(list = existing, inherits = FALSE)
        gc()
        unlink(file3)
    }
)

cat("Runtime handle tests completed!\n")
