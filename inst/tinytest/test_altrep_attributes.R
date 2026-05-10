library(tinytest)
library(Rfmalloc)

message("Testing ALTREP attributes for matrix/array/data.frame objects...")

inspect_payload_offset <- function(x) {
    line <- capture.output(.Internal(inspect(x)))
    line <- line[grepl("fmalloc_altrep", line)]
    if (length(line) < 1L) {
        return(NA_real_)
    }
    as.numeric(sub(".*offset=([0-9]+).*$", "\\1", line[1L]))
}

inspect_runtime_open <- function(x) {
    line <- capture.output(.Internal(inspect(x)))
    line <- line[grepl("fmalloc_altrep", line)]
    if (length(line) < 1L) {
        return(FALSE)
    }
    grepl("runtime=open", line[1L])
}

expect_payload_tracked <- function(runtime, offset) {
    if (is.na(offset)) {
        return(FALSE)
    }
    df <- list_fmalloc_allocations(runtime)
    offset %in% df$payload_offset
}

(function() {
    message("  Test 1: Matrix attributes roundtrip")
    tmp <- tempfile(fileext = ".bin")
    on.exit({
        cleanup_fmalloc(rt)
        unlink(tmp)
    }, add = TRUE)

    rt <- open_fmalloc(tmp)
    v <- create_fmalloc_vector("integer", 6, runtime = rt)
    v[] <- 1:6
    dim(v) <- c(2L, 3L)
    dimnames(v) <- list(c("r1", "r2"), c("c1", "c2", "c3"))

    expect_true(is.matrix(v))
    expect_equal(dim(v), c(2L, 3L))
    expect_equal(dimnames(v)[[1]], c("r1", "r2"))
    expect_equal(dimnames(v)[[2]], c("c1", "c2", "c3"))

    blob <- serialize(v, NULL)
    restored <- unserialize(blob)
    expect_equal(dim(restored), c(2L, 3L))
    expect_equal(dimnames(restored)[[1]], c("r1", "r2"))
    expect_equal(dimnames(restored)[[2]], c("c1", "c2", "c3"))
    expect_equal(as.vector(restored[]), 1:6)
    expect_true(is.integer(restored))

    message("  Matrix attribute roundtrip passed")
})()

(function() {
    message("  Test 2: Array attributes roundtrip")
    tmp <- tempfile(fileext = ".bin")
    on.exit({
        cleanup_fmalloc(rt)
        unlink(tmp)
    }, add = TRUE)

    rt <- open_fmalloc(tmp)
    v <- create_fmalloc_vector("numeric", 24, runtime = rt)
    v[] <- seq_len(24)
    dim(v) <- c(2L, 3L, 4L)
    dimnames(v) <- list(c("x1", "x2"), c("y1", "y2", "y3"), c("z1", "z2", "z3", "z4"))

    expect_true(is.array(v))
    expect_equal(dim(v), c(2L, 3L, 4L))
    expect_equal(dimnames(v)[[3]], c("z1", "z2", "z3", "z4"))

    blob <- serialize(v, NULL)
    restored <- unserialize(blob)
    expect_equal(dim(restored), c(2L, 3L, 4L))
    expect_equal(dimnames(restored)[[3]], c("z1", "z2", "z3", "z4"))
    expect_equal(as.vector(restored[]), seq_len(24))

    message("  Array attribute roundtrip passed")
})()

(function() {
    message("  Test 3: data.frame columns and row.names roundtrip")
    tmp <- tempfile(fileext = ".bin")
    on.exit({
        cleanup_fmalloc(rt)
        unlink(tmp)
    }, add = TRUE)

    rt <- open_fmalloc(tmp)
    a <- create_fmalloc_vector("integer", 3, runtime = rt)
    b <- create_fmalloc_vector("character", 3, runtime = rt)
    a[] <- c(1L, 2L, 3L)
    b[] <- c("aa", "bb", "cc")

    df <- data.frame(a = a, b = b, stringsAsFactors = FALSE)
    expect_equal(class(df), c("data.frame"))
    expect_equal(names(df), c("a", "b"))
    expect_equal(as.integer(row.names(df)), c(1L, 2L, 3L))
    expect_true(all(df$a[] == c(1L, 2L, 3L)))
    expect_equal(as.character(df$b[]), c("aa", "bb", "cc"))

    blob <- serialize(df, NULL)
    restored <- unserialize(blob)
    expect_equal(class(restored), c("data.frame"))
    expect_equal(names(restored), c("a", "b"))
    expect_equal(as.integer(row.names(restored)), c(1L, 2L, 3L))
    expect_equal(restored$a[], c(1L, 2L, 3L))
    expect_equal(restored$b[], c("aa", "bb", "cc"))

    message("  data.frame attribute roundtrip passed")
})()

(function() {
    message("  Test 4: data.frame row.names stay compact for large fmalloc-backed frames")
    tmp <- tempfile(fileext = ".bin")
    on.exit({
        cleanup_fmalloc(rt)
        unlink(tmp)
    }, add = TRUE)

    n <- 5000L
    rt <- open_fmalloc(tmp)
    records_before <- nrow(list_fmalloc_allocations(rt))

    x <- create_fmalloc_vector("integer", n, runtime = rt)
    x[] <- as.integer(seq_len(n))

    df <- as_fmalloc_data_frame(x = x)
    expect_equal(nrow(list_fmalloc_allocations(rt)), records_before + 1L)

    rn <- attr(df, "row.names")
    rn_inspect <- capture.output(.Internal(inspect(rn)))
    expect_true(any(grepl("compact", rn_inspect)) || any(grepl("ALTREP", rn_inspect)))
    expect_equal(length(rn), n)
    expect_true(rn[1L] == 1L)
    expect_true(rn[n] == n)
    expect_true(expect_payload_tracked(rt, inspect_payload_offset(df$x)))

    message("  data.frame row.names metadata test passed")
})()

(function() {
    message("  Test 5: Matrix metadata updates remain fmalloc-backed")
    tmp <- tempfile(fileext = ".bin")
    on.exit({
        cleanup_fmalloc(rt)
        unlink(tmp)
    }, add = TRUE)

    rt <- open_fmalloc(tmp)
    v <- create_fmalloc_vector("integer", 6, runtime = rt)
    v[] <- 1:6

    alias <- v
    offset_before <- inspect_payload_offset(v)
    records_before <- nrow(list_fmalloc_allocations(rt))
    expect_true(inspect_runtime_open(v))
    expect_true(expect_payload_tracked(rt, offset_before))

    dim(v) <- c(2L, 3L)
    offset_dim <- inspect_payload_offset(v)
    expect_true(inspect_runtime_open(v))
    expect_true(expect_payload_tracked(rt, offset_dim))
    expect_true(inspect_payload_offset(alias) == offset_before)

    names(v) <- c("c1", "c2", "c3", "c4", "c5", "c6")
    offset_names <- inspect_payload_offset(v)
    expect_true(inspect_runtime_open(v))
    expect_true(expect_payload_tracked(rt, offset_names))
    expect_true(inspect_payload_offset(alias) == offset_before)

    dimnames(v) <- list(c("r1", "r2"), c("c1", "c2", "c3"))
    offset_dimnames <- inspect_payload_offset(v)
    expect_true(inspect_runtime_open(v))
    expect_true(expect_payload_tracked(rt, offset_dimnames))
    expect_true(inspect_payload_offset(alias) == offset_before)

    expect_true(length(unique(c(offset_before, offset_dim, offset_names, offset_dimnames))) >= 2L)
    expect_true(nrow(list_fmalloc_allocations(rt)) >= records_before)
    expect_equal(unname(v[1L]), 1L)
    expect_equal(unname(alias[1L]), 1L)
    expect_equal(dim(v), c(2L, 3L))
    expect_equal(dimnames(v)[[1]], c("r1", "r2"))
    expect_equal(names(v), c("c1", "c2", "c3", "c4", "c5", "c6"))

    message("  Matrix metadata runtime-residency test passed")
})()

(function() {
    message("  Test 6: data.frame attribute tweaks do not move columns off fmalloc")
    tmp <- tempfile(fileext = ".bin")
    on.exit({
        cleanup_fmalloc(rt)
        unlink(tmp)
    }, add = TRUE)

    rt <- open_fmalloc(tmp)
    a <- create_fmalloc_vector("integer", 3, runtime = rt)
    b <- create_fmalloc_vector("integer", 3, runtime = rt)
    a[] <- c(1L, 2L, 3L)
    b[] <- c(4L, 5L, 6L)

    df <- data.frame(a = a, b = b, stringsAsFactors = FALSE)
    records_before <- nrow(list_fmalloc_allocations(rt))
    a_before <- inspect_payload_offset(df$a)
    b_before <- inspect_payload_offset(df$b)

    row.names(df) <- c("r1", "r2", "r3")
    expect_equal(row.names(df), c("r1", "r2", "r3"))
    expect_equal(df$a[], 1:3)
    expect_equal(df$b[], 4:6)

    attr(df, "note") <- "from_r"
    expect_equal(attr(df, "note"), "from_r")

    a_after <- inspect_payload_offset(df$a)
    b_after <- inspect_payload_offset(df$b)
    expect_true(a_after == a_before)
    expect_true(b_after == b_before)
    expect_true(expect_payload_tracked(rt, a_after))
    expect_true(expect_payload_tracked(rt, b_after))
    expect_true(nrow(list_fmalloc_allocations(rt)) >= records_before)
    expect_true(is.integer(df$a))
    expect_true(is.integer(df$b))

    message("  data.frame metadata update test passed")
})()

message("Attribute tests for ALTREP matrix/array/data.frame completed")
