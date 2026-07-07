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
        expect_true(inherits(x, "fmalloc"))
        x[] <- values[[type]]
        expect_equal(as.vector(x), values[[type]])

        y <- x
        y[1] <- replacements[[type]]
        expect_equal(x[1], values[[type]][1])
        expect_equal(y[1], replacements[[type]])

        z <- x[c(4, 2, NA_integer_, 99)]
        expected <- values[[type]][c(4, 2, NA_integer_, NA_integer_)]
        if (type == "raw") {
            expected <- as.raw(c(4, 2, 0, 0))
        }
        expect_equal(as.vector(z), expected)
        z[1] <- replacements[[type]]
        expect_equal(x[4], values[[type]][4])
        expect_equal(z[1], replacements[[type]])

        # [[ should return scalar values directly without creating a fmalloc object
        scalar <- x[[1]]
        expect_false(inherits(scalar, "fmalloc"))
        expect_equal(length(scalar), 1L)
        expect_equal(scalar, x[1])
    }

    lst <- create_fmalloc_vector("list", 3, runtime = rt)
    expect_true(inherits(lst, "fmalloc"))
    child_int <- create_fmalloc_vector("integer", 2, runtime = rt)
    child_int[] <- 1:2
    child_chr <- create_fmalloc_vector("character", 1, runtime = rt)
    child_chr[] <- "one"
    nested <- create_fmalloc_vector("list", 1, runtime = rt)
    nested_chr <- create_fmalloc_vector("character", 1, runtime = rt)
    nested_chr[] <- "z"
    nested[[1]] <- nested_chr

    lst[[1]] <- child_int
    lst[[2]] <- child_chr
    lst[[3]] <- nested
    expect_error(lst[[1]] <- 1:2, "ordinary R objects")
    expect_error(lst[[1]] <- data.frame(x = 1), "ordinary R objects")
    expect_equal(lst[[1]][], 1:2)
    expect_equal(lst[[2]][], "one")
    expect_equal(lst[[3]][[1]][], "z")

    lst_copy <- lst
    replacement_int <- create_fmalloc_vector("integer", 1, runtime = rt)
    replacement_int[] <- 99L
    lst_copy[[1]] <- replacement_int
    expect_equal(lst[[1]][], 1:2)
    expect_equal(lst_copy[[1]][], 99L)

    lst_subset <- lst[c(3, 1, NA_integer_, 99)]
    expect_equal(lst_subset[[1]][[1]][], "z")
    expect_equal(lst_subset[[2]][], 1:2)
    expect_equal(lst_subset[[3]], NULL)
    expect_equal(lst_subset[[4]], NULL)
    expect_silent(lst_subset[[2]] <- "changed")
    expect_equal(lst_subset[[2]], "changed")
    subset_replacement <- create_fmalloc_vector("character", 1, runtime = rt)
    subset_replacement[] <- "changed"
    lst_subset[[2]] <- subset_replacement
    expect_equal(lst[[1]][], 1:2)

    other_file <- tempfile(fileext = ".bin")
    other_rt <- open_fmalloc(other_file)
    on.exit({
        cleanup_fmalloc(other_rt)
        unlink(other_file)
    }, add = TRUE)
    other_child <- create_fmalloc_vector("integer", 1, runtime = other_rt)
    other_child[] <- 7L
    expect_error(lst[[1]] <- other_child, "same .*fmalloc runtime")

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
    persistent_subset <- persistent[c(4, 1, NA_integer_, 99)]
    expect_equal(persistent_subset[], c(14L, 11L, NA_integer_, NA_integer_))
    persistent_subset_blob <- serialize(persistent_subset, NULL)
    rm(persistent, persistent_subset)
    gc()

    later <- create_fmalloc_vector("integer", 4, runtime = rt)
    later[] <- 101:104

    serialized <- unserialize(persistent_blob)
    expect_true(is.integer(serialized))
    expect_equal(serialized[], 11:14)
    expect_equal(later[], 101:104)

    serialized_subset <- unserialize(persistent_subset_blob)
    expect_equal(serialized_subset[], c(14L, 11L, NA_integer_, NA_integer_))

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
    expect_equal(unclass(scratch_serialized), 21:24)
})()

(function() {
    test_file <- tempfile(fileext = ".bin")
    rt_one <- open_fmalloc(test_file, mode = "scratch", size_gb = 0.05)
    rt_two <- open_fmalloc(test_file, mode = "scratch", size_gb = 0.05)
    expect_error(open_fmalloc(test_file, mode = "persistent"), "already open with mode scratch")
    on.exit({
        cleanup_fmalloc(rt_one)
        cleanup_fmalloc(rt_two)
        unlink(test_file)
    }, add = TRUE)

    list_parent <- create_fmalloc_vector("list", 1, runtime = rt_one)
    child <- create_fmalloc_vector("integer", 1, runtime = rt_two)
    child[] <- 123L

    x <- create_fmalloc_vector("integer", 4, runtime = rt_one)
    x[] <- 1:4
    expect_true(identical(.Call("fmalloc_runtime_of_vector_impl", child),
                           .Call("fmalloc_runtime_of_vector_impl", list_parent)))

    expect_equal(x[c(1L, 0L, 3L)], as.integer(1:4)[c(1L, 0L, 3L)])
    expect_equal(x[c(0L)], integer(0))
    expect_equal(x[c(1.5, 3L)], as.integer(1:4)[c(1.5, 3L)])
    expect_equal(x[as.numeric(c(1, 3))], as.integer(1:4)[as.numeric(c(1, 3))])
    expect_error(x[c(2L, -2L)])
    expect_error(x[[0]], "attempt to select less than one element in get1index")
    expect_error(x[[0L]], "integerOneIndex")
    expect_error(x[[5L]], "subscript out of bounds")
    expect_error(x[[5]], "subscript out of bounds")
})()

message("fmalloc ALTREP vector type tests completed!")
