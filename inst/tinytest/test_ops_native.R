library(Rfmalloc)

message("Testing native C/C++ Ops implementation")

inspect_is_fmalloc <- function(x) {
    l <- capture.output(.Internal(inspect(x)))
    any(grepl("fmalloc_altrep", l))
}

check_op <- function(fm_x, fm_y, op, base_x, base_y = NULL) {
    unary <- is.null(fm_y)
    fm_result <- if (unary) op(fm_x) else op(fm_x, fm_y)
    base_result <- if (unary) op(base_x) else op(base_x, base_y)
    expect_true(inherits(fm_result, "fmalloc"))
    expect_true(inspect_is_fmalloc(fm_result))
    expect_equal(as.vector(fm_result), as.vector(base_result))
    invisible(fm_result)
}

rt_file <- tempfile(fileext = ".bin")
rt <- open_fmalloc(rt_file, mode = "scratch", size_gb = 0.1)

make_fm <- function(type, values) {
    v <- create_fmalloc_vector(type, length(values), runtime = rt)
    v[] <- values
    v
}

# Test 1: Integer arithmetic
message("Test 1: Integer arithmetic")
fi_a <- make_fm("integer", c(10L, 20L, 30L, NA_integer_, 50L))
fi_b <- make_fm("integer", c(1L, 2L, 3L, 4L, 5L))
bi_a <- c(10L, 20L, 30L, NA_integer_, 50L)
bi_b <- c(1L, 2L, 3L, 4L, 5L)
check_op(fi_a, fi_b, `+`, bi_a, bi_b)
check_op(fi_a, fi_b, `-`, bi_a, bi_b)
check_op(fi_a, fi_b, `*`, bi_a, bi_b)
check_op(fi_a, fi_b, `%/%`, bi_a, bi_b)
check_op(fi_a, fi_b, `%%`, bi_a, bi_b)
check_op(fi_a, 100L, `+`, bi_a, 100L)
check_op(100L, fi_a, `+`, 100L, bi_a)
check_op(fi_a, 5L, `-`, bi_a, 5L)
check_op(fi_a, 3L, `*`, bi_a, 3L)
check_op(fi_a, bi_b, `+`, bi_a, bi_b)
check_op(bi_a, fi_b, `+`, bi_a, bi_b)
r_add <- fi_a + fi_b
expect_true(is.na(as.vector(r_add)[4]))
message("  Integer arithmetic passed")

# Test 2: Integer comparisons
message("Test 2: Integer comparisons")
check_op(fi_a, fi_b, `==`, bi_a, bi_b)
check_op(fi_a, fi_b, `!=`, bi_a, bi_b)
check_op(fi_a, fi_b, `<`,  bi_a, bi_b)
check_op(fi_a, fi_b, `<=`, bi_a, bi_b)
check_op(fi_a, fi_b, `>`,  bi_a, bi_b)
check_op(fi_a, fi_b, `>=`, bi_a, bi_b)
r_eq <- fi_a == fi_b
expect_equal(as.vector(r_eq), as.vector(bi_a == bi_b))
expect_true(is.na(as.vector(r_eq)[4]))
message("  Integer comparisons passed")

# Test 3: Real arithmetic
message("Test 3: Real arithmetic")
fd_a <- make_fm("numeric", c(1.5, 2.5, NA_real_, 10.0, 0.0))
fd_b <- make_fm("numeric", c(0.5, 2.0, 3.0, NA_real_, 2.0))
bd_a <- c(1.5, 2.5, NA_real_, 10.0, 0.0)
bd_b <- c(0.5, 2.0, 3.0, NA_real_, 2.0)
check_op(fd_a, fd_b, `+`, bd_a, bd_b)
check_op(fd_a, fd_b, `-`, bd_a, bd_b)
check_op(fd_a, fd_b, `*`, bd_a, bd_b)
check_op(fd_a, fd_b, `/`, bd_a, bd_b)
check_op(fd_a, fd_b, `^`, bd_a, bd_b)
message("  Real arithmetic passed")

# Test 4: Mixed types
message("Test 4: Mixed types")
fi_three <- make_fm("integer", c(2L, 3L, 4L, 5L, 6L))
bi_three <- c(2L, 3L, 4L, 5L, 6L)
check_op(bd_a, bi_three, `/`, bd_a, bi_three)
check_op(bi_three, bd_a, `/`, bi_three, bd_a)
r_mixed <- fi_a + fd_a
expect_equal(as.vector(r_mixed), as.vector(bi_a + bd_a))
message("  Mixed types passed")

# Test 5: Logical operators
message("Test 5: Logical operators")
fl_a <- make_fm("logical", c(TRUE, FALSE, NA, TRUE, FALSE))
fl_b <- make_fm("logical", c(TRUE, TRUE, FALSE, NA, TRUE))
bl_a <- c(TRUE, FALSE, NA, TRUE, FALSE)
bl_b <- c(TRUE, TRUE, FALSE, NA, TRUE)
check_op(fl_a, fl_b, `&`, bl_a, bl_b)
check_op(fl_a, fl_b, `|`, bl_a, bl_b)
check_op(fl_a, fl_b, `==`, bl_a, bl_b)
check_op(fl_a, fl_b, `!=`, bl_a, bl_b)
r_not <- !fl_a
expect_equal(as.vector(r_not), as.vector(!bl_a))
message("  Logical operators passed")

# Test 6: Unary operators
message("Test 6: Unary operators")
check_op(fi_a, NULL, function(x) -x, bi_a)
check_op(fi_a, NULL, function(x) +x, bi_a)
check_op(fd_a, NULL, function(x) -x, bd_a)
check_op(fd_a, NULL, function(x) +x, bd_a)
r_neg <- -fi_a
expect_equal(as.vector(r_neg), as.vector(-bi_a))
message("  Unary operators passed")

# Test 7: Integer division/power
message("Test 7: Integer division/power")
fi_div_a <- make_fm("integer", c(10L, 9L, 8L, NA_integer_, 5L))
fi_div_b <- make_fm("integer", c(3L, 2L, 4L, 2L, 0L))
bi_div_a <- c(10L, 9L, 8L, NA_integer_, 5L)
bi_div_b <- c(3L, 2L, 4L, 2L, 0L)
check_op(fi_div_a, fi_div_b, `/`, bi_div_a, bi_div_b)
check_op(fi_a, fi_b, `^`, bi_a, bi_b)
message("  Integer division/power passed")

# Test 8: Recycling
message("Test 8: Recycling")
fi_len5 <- make_fm("integer", 1:5); bi_len5 <- 1:5
expect_equal(as.vector(fi_len5 + 10L), bi_len5 + 10L)
expect_equal(as.vector(10L + fi_len5), 10L + bi_len5)
expect_equal(as.vector(fi_len5 + 3.14), bi_len5 + 3.14)
fi_short <- make_fm("integer", c(1L, 2L))
fi_long <- make_fm("integer", c(10L, 20L, 30L, 40L))
expect_silent(fi_long + fi_short)
expect_equal(as.vector(fi_long + fi_short), c(11L, 22L, 31L, 42L))
fi_med <- make_fm("integer", c(1L, 2L, 3L))
expect_warning(fi_len5 + fi_med, "longer object length")
expect_equal(as.vector(suppressWarnings(fi_len5 + fi_med)),
             as.vector(suppressWarnings(bi_len5 + c(1L, 2L, 3L))))
message("  Recycling passed")

# Test 9: Matrix dim preservation
message("Test 9: Matrix dimension preservation")
m <- create_fmalloc_matrix("integer", nrow = 2L, ncol = 3L, runtime = rt)
m[] <- 1:6; base_m <- matrix(1:6, nrow = 2L, ncol = 3L)
expect_true(inherits(m + 10L, "fmalloc_matrix"))
expect_equal(as.vector(m + 10L), as.vector(base_m + 10L))
expect_equal(dim(m + 10L), dim(base_m + 10L))
expect_true(inherits(m + m, "fmalloc_matrix"))
expect_equal(as.vector(m + m), as.vector(base_m + base_m))
expect_true(inherits(-m, "fmalloc_matrix"))
expect_equal(as.vector(-m), as.vector(-base_m))
expect_equal(dim(-m), dim(-base_m))
expect_true(inherits(m + c(10L, 20L, 30L), "fmalloc_matrix"))
message("  Matrix dimension preservation passed")

# Test 10: Zero-length
message("Test 10: Zero-length vectors")
fz <- create_fmalloc_vector("integer", 0, runtime = rt)
expect_equal(length(fz + 1L), 0L); expect_true(inherits(fz + 1L, "fmalloc"))
expect_equal(length(fz + fz), 0L)
fz_r <- create_fmalloc_vector("numeric", 0, runtime = rt)
expect_equal(length(fz_r + 1.0), 0L)
expect_equal(length(fz + fz_r), 0L)
message("  Zero-length vectors passed")

# Test 11: Mixed fmalloc + base
message("Test 11: Mixed fmalloc + base")
fi_mx <- make_fm("integer", 1:5); bi_mx <- 1:5
expect_equal(as.vector(fi_mx + bi_mx), bi_mx + bi_mx)
expect_equal(as.vector(bi_mx + fi_mx), bi_mx + bi_mx)
expect_equal(as.vector(fi_mx + c(1.5, 2.5, 3.5, 4.5, 5.5)), bi_mx + c(1.5, 2.5, 3.5, 4.5, 5.5))
expect_equal(as.vector(fi_mx + fi_mx + bi_mx), bi_mx + bi_mx + bi_mx)
message("  Mixed fmalloc + base passed")

# Test 12: GC safety
message("Test 12: GC safety")
fi_gc <- make_fm("integer", 1:100)
for (i in 1:3) {
    r <- fi_gc + 100L
    expect_equal(as.vector(r), 1:100 + 100L)
    gc(); expect_true(inherits(r, "fmalloc"))
    expect_equal(as.vector(r), 1:100 + 100L)
}
message("  GC safety passed")

# Test 13: Large vector stress
message("Test 13: Large vector stress test")
n_big <- 100000L
fi_big <- make_fm("integer", seq_len(n_big))
fi_big2 <- make_fm("integer", rev(seq_len(n_big)))
r_add <- fi_big + fi_big2
expect_true(inherits(r_add, "fmalloc"))
expect_equal(as.vector(r_add[1:5]), as.vector(c(1+n_big, 2+(n_big-1), 3+(n_big-2), 4+(n_big-3), 5+(n_big-4))))
r_mul <- fi_big * 3L
expect_equal(as.vector(r_mul[1:5]), c(3L, 6L, 9L, 12L, 15L))
r_cmp <- fi_big > (n_big / 2)
expect_true(inherits(r_cmp, "fmalloc"))
expect_equal(sum(r_cmp, na.rm = TRUE), n_big / 2)
r_neg <- -fi_big
expect_equal(as.vector(r_neg[1:5]), -(1:5))
fd_big <- make_fm("numeric", seq_len(n_big) * 1.5)
r_real <- fd_big + 0.5
expect_equal(as.vector(r_real[1:3]), c(2.0, 3.5, 5.0))
gc()
expect_equal(as.vector(r_add[1:5]), as.vector(c(1+n_big, 2+(n_big-1), 3+(n_big-2), 4+(n_big-3), 5+(n_big-4))))
expect_equal(as.vector(r_mul[1:5]), c(3L, 6L, 9L, 12L, 15L))
message("  Large vector stress test passed")

# Test 14: ALTREP output
message("Test 14: ALTREP output verification")
fi_v <- make_fm("integer", 1:10)
fl_v <- make_fm("logical", rep(c(TRUE, FALSE), 5))
expect_true(inherits(fi_v + 1L, "fmalloc")); expect_true(inherits(fi_v - 1L, "fmalloc"))
expect_true(inherits(fi_v * 2L, "fmalloc")); expect_true(inherits(fi_v / 2L, "fmalloc"))
expect_true(inherits(fi_v ^ 2L, "fmalloc")); expect_true(inherits(fi_v %% 3L, "fmalloc"))
expect_true(inherits(fi_v %/% 3L, "fmalloc"))
expect_true(inherits(fi_v == 5L, "fmalloc")); expect_true(inherits(fi_v != 5L, "fmalloc"))
expect_true(inherits(fi_v < 5L, "fmalloc")); expect_true(inherits(fi_v > 5L, "fmalloc"))
expect_true(inherits(fi_v <= 5L, "fmalloc")); expect_true(inherits(fi_v >= 5L, "fmalloc"))
expect_true(inherits(fl_v & TRUE, "fmalloc")); expect_true(inherits(fl_v | FALSE, "fmalloc"))
expect_true(inherits(!fl_v, "fmalloc")); expect_true(inherits(-fi_v, "fmalloc"))
expect_true(inherits(+fi_v, "fmalloc"))
message("  ALTREP output verification passed")

message("All native ops tests completed!")

cleanup_fmalloc(rt)
unlink(rt_file)
