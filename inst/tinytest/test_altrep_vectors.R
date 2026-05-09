library(tinytest)
library(Rfmalloc)

cat("Testing fmalloc ALTREP vector types and duplication...\n")

is_fm_altrep <- function(x) .Call("is_fmalloc_altrep_impl", x, PACKAGE = "Rfmalloc")

test_file <- tempfile(fileext = ".bin")
tryCatch(
    {
        rt <- open_fmalloc(test_file, size_gb = 0.05)

        values <- list(
            logical = c(TRUE, FALSE, NA, TRUE),
            integer = 1:4,
            numeric = c(1.5, 2.5, 3.5, 4.5),
            raw = as.raw(1:4),
            complex = c(1+1i, 2+2i, 3+3i, 4+4i),
            character = c("a", "b", "c", "d")
        )
        replacements <- list(
            logical = FALSE,
            integer = 99L,
            numeric = 99.5,
            raw = as.raw(99),
            complex = 99+99i,
            character = "z"
        )

        for (type in names(values)) {
            x <- create_fmalloc_vector(type, length(values[[type]]), runtime = rt)
            expect_true(is_fm_altrep(x))
            expect_equal(attributes(x), NULL)
            x[] <- values[[type]]
            expect_equal(as.vector(x), values[[type]])

            y <- x
            y[1] <- replacements[[type]]
            expect_true(is_fm_altrep(y))
            expect_equal(x[1], values[[type]][1])
            expect_equal(y[1], replacements[[type]])
        }

        lst <- create_fmalloc_vector("list", 3, runtime = rt)
        expect_true(is_fm_altrep(lst))
        expect_equal(attributes(lst), NULL)
        lst[[1]] <- 1:2
        lst[[2]] <- data.frame(x = 1)
        lst[[3]] <- list(z = "z")
        expect_equal(lst[[1]], 1:2)
        expect_equal(lst[[2]]$x, 1)
        expect_equal(lst[[3]]$z, "z")

        lst_copy <- lst
        lst_copy[[1]] <- 99L
        expect_true(is_fm_altrep(lst_copy))
        expect_equal(lst[[1]], 1:2)
        expect_equal(lst_copy[[1]], 99L)

        serialized <- unserialize(serialize(create_fmalloc_vector("integer", 2, runtime = rt), NULL))
        expect_true(is.integer(serialized))
        expect_false(is_fm_altrep(serialized))

        cleanup_fmalloc(rt)
        rm(rt, x, y, lst, lst_copy, serialized)
        gc()
    },
    finally = {
        cleanup_fmalloc()
        unlink(test_file)
    }
)

cat("fmalloc ALTREP vector type tests completed!\n")
