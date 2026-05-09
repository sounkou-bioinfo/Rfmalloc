# Test fmalloc ALTREP vector functionality

library(tinytest)
library(Rfmalloc)

message("Testing fmalloc ALTREP vector functionality...")

# Test error conditions first (these should work without fmalloc initialization)
expect_error(create_fmalloc_vector("integer", 50), "fmalloc not initialized")
expect_error(create_fmalloc_vector("integer", -1), "positive integer")
expect_error(create_fmalloc_vector("integer", 2.5), "positive integer or zero")
expect_error(create_fmalloc_vector("integer", NA_real_), "positive integer or zero")
expect_error(create_fmalloc_vector("integer", Inf), "positive integer or zero")
expect_error(
    create_fmalloc_vector("integer", .Machine$integer.max + 1),
    "too large"
)
expect_error(create_fmalloc_vector(123, 50), "character string")
expect_error(
    create_fmalloc_vector("invalid_type", 50),
    "Unsupported vector type"
)
expect_error(init_fmalloc(123), "character string")
expect_error(init_fmalloc(""), "cannot be empty")

message("Input validation tests passed!")

(function() {
    test_file <- tempfile(fileext = ".bin")
    on.exit({
        cleanup_fmalloc()
        unlink(test_file)
    }, add = TRUE)

    init_result <- init_fmalloc(test_file)
    expect_true(is.logical(init_result))
    message("fmalloc initialization successful!")

    v_zero <- create_fmalloc_vector("integer", 0)
    expect_true(is.integer(v_zero))
    expect_equal(length(v_zero), 0)

    v_int <- create_fmalloc_vector("integer", 50)
    expect_true(is.integer(v_int))
    expect_equal(length(v_int), 50)

    v_num <- create_fmalloc_vector("numeric", 30)
    expect_true(is.numeric(v_num))
    expect_equal(length(v_num), 30)

    v_log <- create_fmalloc_vector("logical", 20)
    expect_true(is.logical(v_log))
    expect_equal(length(v_log), 20)

    v_raw <- create_fmalloc_vector("raw", 8)
    expect_true(is.raw(v_raw))
    expect_equal(length(v_raw), 8)

    v_cplx <- create_fmalloc_vector("complex", 6)
    expect_true(is.complex(v_cplx))
    expect_equal(length(v_cplx), 6)

    v_chr <- create_fmalloc_vector("character", 4)
    expect_true(is.character(v_chr))
    expect_equal(length(v_chr), 4)

    v_lst <- create_fmalloc_vector("list", 3)
    expect_true(is.list(v_lst))
    expect_equal(length(v_lst), 3)

    v_int[1:10] <- 1:10
    expect_equal(v_int[1:10], 1:10)

    v_num[1:5] <- c(1.1, 2.2, 3.3, 4.4, 5.5)
    expect_equal(v_num[1:5], c(1.1, 2.2, 3.3, 4.4, 5.5), tolerance = 1e-10)

    v_log[1:3] <- c(TRUE, FALSE, TRUE)
    expect_equal(v_log[1:3], c(TRUE, FALSE, TRUE))

    v_raw[] <- as.raw(1:8)
    expect_equal(v_raw, as.raw(1:8))

    v_cplx[] <- c(1+1i, 2+2i, 3+3i, 4+4i, 5+5i, 6+6i)
    expect_equal(v_cplx[1:3], c(1+1i, 2+2i, 3+3i))

    v_chr[] <- c("a", "b", NA_character_, "d")
    expect_equal(v_chr[], c("a", "b", NA_character_, "d"))

    list_child <- create_fmalloc_vector("integer", 2)
    list_child[] <- 1:2
    list_replacement <- create_fmalloc_vector("integer", 1)
    list_replacement[] <- 99L

    v_lst[[1]] <- list_child
    v_lst[2] <- list(NULL)
    expect_error(v_lst[[3]] <- 1:2, "ordinary R objects")
    expect_error(v_lst[[3]] <- data.frame(x = 1), "ordinary R objects")
    expect_equal(v_lst[[1]][], 1:2)
    expect_equal(v_lst[[2]], NULL)

    v_dup <- v_lst
    v_dup[[1]] <- list_replacement
    expect_equal(v_lst[[1]][], 1:2)
    expect_equal(v_dup[[1]][], 99L)

    rm(v_zero, v_int, v_num, v_log, v_raw, v_cplx, v_chr, v_lst, v_dup, list_child, list_replacement)
    gc()

    cleanup_fmalloc()
    expect_error(
        create_fmalloc_vector("integer", 5),
        "fmalloc not initialized"
    )

    message("fmalloc ALTREP vector tests passed!")
})()

message("fmalloc test suite completed!")
