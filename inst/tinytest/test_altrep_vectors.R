library(tinytest)
library(Rfmalloc)

message("Testing fmalloc ALTREP vector types and duplication...")

(function() {
    test_file <- tempfile(fileext = ".bin")
    rt <- open_fmalloc(test_file, size_gb = 0.05)
    on.exit({
        cleanup_fmalloc(rt)
        unlink(test_file)
    }, add = TRUE)

    values <- list(
        logical = c(TRUE, FALSE, NA, TRUE),
        integer = 1:4,
        numeric = c(1.5, 2.5, 3.5, 4.5),
        raw = as.raw(1:4),
        complex = c(1+1i, 2+2i, 3+3i, 4+4i),
        character = c("alpha", "beta", NA_character_, "delta")
    )
    replacements <- list(
        logical = FALSE,
        integer = 99L,
        numeric = 99.5,
        raw = as.raw(99),
        complex = 99+99i,
        character = "zeta"
    )

    for (type in names(values)) {
        x <- create_fmalloc_vector(type, length(values[[type]]), runtime = rt)
        expect_equal(attributes(x), NULL)
        x[] <- values[[type]]
        expect_equal(as.vector(x), values[[type]])

        y <- x
        y[1] <- replacements[[type]]
        expect_equal(x[1], values[[type]][1])
        expect_equal(y[1], replacements[[type]])
    }

    lst <- create_fmalloc_vector("list", 3, runtime = rt)
    expect_equal(attributes(lst), NULL)
    lst[[1]] <- 1:2
    lst[[2]] <- data.frame(x = 1)
    lst[[3]] <- list(z = "z")
    expect_equal(lst[[1]], 1:2)
    expect_equal(lst[[2]]$x, 1)
    expect_equal(lst[[3]]$z, "z")

    lst_copy <- lst
    lst_copy[[1]] <- 99L
    expect_equal(lst[[1]], 1:2)
    expect_equal(lst_copy[[1]], 99L)

    coerce_src <- create_fmalloc_vector("integer", 4, runtime = rt)
    coerce_src[] <- 1:4
    coerce_num <- as.numeric(coerce_src)
    expect_true(is.double(coerce_num))
    expect_equal(coerce_num[], as.numeric(1:4))
    coerce_chr <- as.character(coerce_src)
    expect_true(is.character(coerce_chr))
    expect_equal(coerce_chr[], as.character(1:4))

    persistent <- create_fmalloc_vector("integer", 4, runtime = rt)
    persistent[] <- 11:14
    persistent_blob <- serialize(persistent, NULL)
    rm(persistent)
    gc()

    later <- create_fmalloc_vector("integer", 4, runtime = rt)
    later[] <- 101:104

    serialized <- unserialize(persistent_blob)
    expect_true(is.integer(serialized))
    expect_equal(serialized[], 11:14)
    expect_equal(later[], 101:104)

    persistent_chr <- create_fmalloc_vector("character", 3, runtime = rt)
    persistent_chr[] <- c("one", NA_character_, "three")
    persistent_chr_blob <- serialize(persistent_chr, NULL)
    rm(persistent_chr)
    gc()
    serialized_chr <- unserialize(persistent_chr_blob)
    expect_equal(serialized_chr[], c("one", NA_character_, "three"))

    scratch_file <- tempfile(fileext = ".bin")
    scratch_rt <- open_fmalloc(scratch_file, mode = "scratch")
    on.exit({
        cleanup_fmalloc(scratch_rt)
        unlink(scratch_file)
    }, add = TRUE)

    scratch <- create_fmalloc_vector("integer", 4, runtime = scratch_rt)
    scratch[] <- 21:24
    scratch_serialized <- unserialize(serialize(scratch, NULL))
    expect_true(is.integer(scratch_serialized))
    expect_equal(scratch_serialized, 21:24)
})()

message("fmalloc ALTREP vector type tests completed!")
