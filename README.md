# Rfmalloc

<!-- badges: start -->
[![R-CMD-check](https://github.com/sounkou-bioinfo/Rfmalloc/actions/workflows/R-CMD-check.yaml/badge.svg)](https://github.com/sounkou-bioinfo/Rfmalloc/actions/workflows/R-CMD-check.yaml)
<!-- badges: end -->

Rfmalloc is an experimental R package for file-backed R vectors using a
patched copy of [fmalloc](https://github.com/yasukata/fmalloc). The
current R-facing vector API creates fmalloc-backed ALTREP vectors for
atomic, character, and list storage.

Earlier prototypes explored R’s custom allocator / `Rf_allocVector3()`
path (see Simon Urbanek’s
[proof-of-concept](https://gist.github.com/s-u/6712c97ca74181f5a1a5)).
The package has moved to direct ALTREP vectors for the public vector API
and control over copy-on-write duplication. The patched fmalloc
`malloc`/`free`/`realloc` layer is still built and installed for native
consumers that use the C API or link against the bundled library.

## Current Status

Implemented now:

- fmalloc-backed ALTREP vectors for logical, integer, numeric, raw,
  complex, character, and list values; list elements are restricted to
  `NULL` or other Rfmalloc vectors from the same runtime;
- explicit runtime handles and `persistent` / `scratch` runtime modes;
- persistent serialization and reopening for fixed-width atomic and
  character vectors;
- fmalloc-backed coercion and subset-copy results for supported vector
  types;
- nested list-container recovery by reference for persistent runtimes;
- explicit `destroy_fmalloc_vector()` for immediate vector
  invalidation/cleanup with parent-reference safety checks;
- an in-file persistent allocation catalog used to validate serialized
  references;
- an installed C header and `R_RegisterCCallable()` API for other
  packages.

Known limitations:

- Core `Ops`, `Summary`, `Math`, and `Math2` workflows are dispatched
  through S3 methods for common vector/matrix paths.
- Explicit base-fallback boundaries:
  - `rowSums()`, `colSums()`, `rowMeans()`, and `colMeans()` fall back
    to base implementations (with a warning) when the input is not an
    exact 2D matrix or when `dims != 1L`.
  - Scalar or zero-length outputs from `Summary`, `Math`, or `Math2`
    (for example `sum(x)` returning a single value) are ordinary R
    scalars by design.
- Full operator- and method-family coverage is still incomplete;
  additional edge cases may still materialize ordinary R objects.

Still experimental: richer catalog and inspection tooling, and
mapped-file recovery diagnostics.

## Installation

``` r
# install.packages("devtools")
devtools::install_github("sounkou-bioinfo/Rfmalloc")
```

## Basic Usage

The examples use temporary backing files. Each snippet runs inside
`local()` so its `on.exit()` cleanup stays scoped to the snippet while
the README is rendered; this is not required in normal interactive use.
In your own code, keep the runtime handle you need and call
`cleanup_fmalloc()` when finished.

The shortest form uses `init_fmalloc()`, which opens a backing file and
stores it as the package default runtime. Calls to
`create_fmalloc_vector()` can then omit the `runtime` argument:

``` r
library(Rfmalloc)
local({
  alloc_file <- tempfile(fileext = ".bin")
  init_fmalloc(alloc_file)
  on.exit({
    cleanup_fmalloc()
    unlink(alloc_file)
  }, add = TRUE)

  v_int <- create_fmalloc_vector("integer", 10)
  v_num <- create_fmalloc_vector("numeric", 10)
  v_raw <- create_fmalloc_vector("raw", 4)
  v_cplx <- create_fmalloc_vector("complex", 4)
  v_chr <- create_fmalloc_vector("character", 3)
  v_lst <- create_fmalloc_vector("list", 2)

  v_int[1:3] <- c(1L, 2L, 3L)
  v_num[1:3] <- c(1.1, 2.2, 3.3)
  v_raw[] <- as.raw(1:4)
  v_cplx[] <- c(1+1i, 2+2i, 3+3i, 4+4i)
  v_chr[] <- c("alpha", "beta", NA_character_)

  list_child <- create_fmalloc_vector("integer", 2)
  list_child[] <- 1:2
  v_lst[[1]] <- list_child

  list(
    integer = v_int[1:3],
    numeric = v_num[1:3],
    raw = v_raw[],
    complex = v_cplx[],
    character = v_chr[],
    list_first = v_lst[[1]][]
  )
})
#> $integer
#> [1] 1 2 3
#> 
#> $numeric
#> [1] 1.1 2.2 3.3
#> 
#> $raw
#> [1] 01 02 03 04
#> 
#> $complex
#> [1] 1+1i 2+2i 3+3i 4+4i
#> 
#> $character
#> [1] "alpha" "beta"  NA     
#> 
#> $list_first
#> [1] 1 2
```

For multiple backing files or explicit lifetime management, prefer
runtime handles returned by `open_fmalloc()`:

``` r

local({
  handle_file <- tempfile(fileext = ".bin")
  rt <- open_fmalloc(handle_file)
  on.exit({
    cleanup_fmalloc(rt)
    unlink(handle_file)
  }, add = TRUE)

  v <- create_fmalloc_vector("integer", 10, runtime = rt)
  v[1:3] <- 10:12
  v[1:3]
})
#> [1] 10 11 12
```

## Larger Allocation Example

This example is executed when building the README and uses a 1B-length
fmalloc payload.

``` r
local({
  large_file <- tempfile(fileext = ".bin")
  rt <- open_fmalloc(large_file, size_gb = 10)
  on.exit({
    cleanup_fmalloc(rt)
    unlink(large_file)
  }, add = TRUE)

  n <- 10000000L
  before_alloc <- nrow(list_fmalloc_allocations(rt))

  big_int_a <- create_fmalloc_vector("integer", n, runtime = rt)
  big_int_b <- create_fmalloc_vector("integer", n, runtime = rt)
  big_int_a[1:5] <- 1:5
  big_int_b[1:5] <- 6:10

  # Elementwise arithmetic stays on fmalloc-managed ALTREP for the full-length result
  big_sum <- big_int_a + big_int_b
  after_alloc <- nrow(list_fmalloc_allocations(rt))

  list(
    a_head = big_int_a[1:5],
    b_head = big_int_b[1:5],
    sum_head = big_sum[1:5],
    sum_length = length(big_sum),
    sum_managed = inherits(big_sum, "fmalloc") &&
      grepl("fmalloc_altrep", capture.output(.Internal(inspect(big_sum)))[[1L]], fixed = TRUE),
    allocations_delta = after_alloc - before_alloc
  )

  # sum(big_sum) is not used here: Summary(S) generics return scalars.
})
#> $a_head
#> [1] 1 2 3 4 5
#> 
#> $b_head
#> [1]  6  7  8  9 10
#> 
#> $sum_head
#> [1]  7  9 11 13 15
#> 
#> $sum_length
#> [1] 10000000
#> 
#> $sum_managed
#> [1] TRUE
#> 
#> $allocations_delta
#> [1] 3
```

## Arithmetic and comparison operations

Elementwise `Ops` (`+`, `-`, `*`, `/`, `^`, `%%`, `%/%`, `==`, `!=`,
`<`, `>`, `<=`, `>=`, `&`, `|`, `!`, unary `-` and `+`) on fmalloc
vectors are implemented as native C/C++ kernels. Results stay in the
same fmalloc runtime for large vectors.

``` r
local({
  ops_file <- tempfile(fileext = ".bin")
  rt <- open_fmalloc(ops_file, mode = "scratch", size_gb = 0.1)
  on.exit({
    cleanup_fmalloc(rt)
    unlink(ops_file)
  }, add = TRUE)

  x <- create_fmalloc_vector("integer", 5, runtime = rt)
  y <- create_fmalloc_vector("integer", 5, runtime = rt)
  x[] <- c(10L, 20L, 30L, NA_integer_, 50L)
  y[] <- 1:5

  list(
    add = x + y,
    sub = x - y,
    mul = x * y,
    div = x / y,
    idiv = x %/% y,
    mod = x %% y,
    pow = x ^ 2L,
    cmp_eq = x == y,
    cmp_lt = x < 20L,
    unary_neg = -x,
    unary_not = !(x > 20L)
  )
})
#> $add
#> [1] 11 22 33 NA 55
#> attr(,"class")
#> [1] "fmalloc_vector" "fmalloc"        "integer"       
#> 
#> $sub
#> [1]  9 18 27 NA 45
#> attr(,"class")
#> [1] "fmalloc_vector" "fmalloc"        "integer"       
#> 
#> $mul
#> [1]  10  40  90  NA 250
#> attr(,"class")
#> [1] "fmalloc_vector" "fmalloc"        "integer"       
#> 
#> $div
#> [1] 10 10 10 NA 10
#> attr(,"class")
#> [1] "fmalloc_vector" "fmalloc"        "numeric"       
#> 
#> $idiv
#> [1] 10 10 10 NA 10
#> attr(,"class")
#> [1] "fmalloc_vector" "fmalloc"        "integer"       
#> 
#> $mod
#> [1]  0  0  0 NA  0
#> attr(,"class")
#> [1] "fmalloc_vector" "fmalloc"        "integer"       
#> 
#> $pow
#> [1]  100  400  900   NA 2500
#> attr(,"class")
#> [1] "fmalloc_vector" "fmalloc"        "numeric"       
#> 
#> $cmp_eq
#> [1] FALSE FALSE FALSE    NA FALSE
#> attr(,"class")
#> [1] "fmalloc_vector" "fmalloc"        "logical"       
#> 
#> $cmp_lt
#> [1]  TRUE FALSE FALSE    NA FALSE
#> attr(,"class")
#> [1] "fmalloc_vector" "fmalloc"        "logical"       
#> 
#> $unary_neg
#> [1] -10 -20 -30  NA -50
#> attr(,"class")
#> [1] "fmalloc_vector" "fmalloc"        "integer"       
#> 
#> $unary_not
#> [1]  TRUE  TRUE FALSE    NA FALSE
#> attr(,"class")
#> [1] "fmalloc_vector" "fmalloc"        "logical"
```

Mixed fmalloc + base vector and scalar recycling work:

``` r
local({
  mix_file <- tempfile(fileext = ".bin")
  rt <- open_fmalloc(mix_file, mode = "scratch", size_gb = 0.1)
  on.exit({
    cleanup_fmalloc(rt)
    unlink(mix_file)
  }, add = TRUE)

  x <- create_fmalloc_vector("numeric", 4, runtime = rt)
  x[] <- c(1.5, 2.5, 3.5, 4.5)

  list(
    fm_plus_scalar = x + 10,
    scalar_plus_fm = 10 + x,
    fm_plus_base   = x + c(100, 200, 300, 400),
    base_plus_fm   = c(1, 2, 3, 4) + x,
    recycling_ok   = x + c(10, 20),
    logical_op     = (x > 2) & (x < 5)
  )
})
#> $fm_plus_scalar
#> [1] 11.5 12.5 13.5 14.5
#> attr(,"class")
#> [1] "fmalloc_vector" "fmalloc"        "numeric"       
#> 
#> $scalar_plus_fm
#> [1] 11.5 12.5 13.5 14.5
#> attr(,"class")
#> [1] "fmalloc_vector" "fmalloc"        "numeric"       
#> 
#> $fm_plus_base
#> [1] 101.5 202.5 303.5 404.5
#> attr(,"class")
#> [1] "fmalloc_vector" "fmalloc"        "numeric"       
#> 
#> $base_plus_fm
#> [1] 2.5 4.5 6.5 8.5
#> attr(,"class")
#> [1] "fmalloc_vector" "fmalloc"        "numeric"       
#> 
#> $recycling_ok
#> [1] 11.5 22.5 13.5 24.5
#> attr(,"class")
#> [1] "fmalloc_vector" "fmalloc"        "numeric"       
#> 
#> $logical_op
#> [1] FALSE  TRUE  TRUE  TRUE
#> attr(,"class")
#> [1] "fmalloc_vector" "fmalloc"        "logical"
```

## Explicit destruction and parent safety

`destroy_fmalloc_vector()` immediately releases native bookkeeping for
one fmalloc ALTREP vector and returns a logical indicating whether a
live vector was destroyed. The helper enforces parent-reference safety:
a vector cannot be destroyed while it is still stored as a child of any
fmalloc list. When needed, drop parent links first.

``` r

local({
  destroy_file <- tempfile(fileext = ".bin")
  rt <- open_fmalloc(destroy_file, mode = "persistent")
  on.exit({
    cleanup_fmalloc(rt)
    unlink(destroy_file)
  }, add = TRUE)

  child <- create_fmalloc_vector("integer", 2, runtime = rt)
  child[] <- 1:2

  parent <- create_fmalloc_vector("list", 1, runtime = rt)
  parent[[1]] <- child

  destroy_error <- try(destroy_fmalloc_vector(child), silent = TRUE)

  parent[[1]] <- NULL
  destroy_ok <- destroy_fmalloc_vector(child)

  list(
    destroy_rejected_with_parent = inherits(destroy_error, "try-error"),
    destroy_succeeded_after_unset = destroy_ok
  )
})
#> $destroy_rejected_with_parent
#> [1] TRUE
#> 
#> $destroy_succeeded_after_unset
#> [1] TRUE
```

In persistent mode, destroying with default semantics retains payload
bytes so the recorded on-disk allocation remains recoverable by normal
`serialize()` flows. Use `destroy_fmalloc_vector(x, unsafe = TRUE)` to
reclaim persistent payload memory and mark the catalog entry as
non-recoverable.

This is scoped to the targeted vector(s): if one object is
unsafe-destroyed, other objects in the same runtime remain recoverable.

``` r

local({
  selective_file <- tempfile(fileext = ".bin")
  rt <- open_fmalloc(selective_file, mode = "persistent")
  on.exit({
    cleanup_fmalloc(rt)
    unlink(selective_file)
  }, add = TRUE)

  keep <- create_fmalloc_vector("integer", 3, runtime = rt)
  drop <- create_fmalloc_vector("character", 2, runtime = rt)
  keep[] <- c(101L, 202L, 303L)
  drop[] <- c("alpha", "beta")

  keep_blob <- serialize(keep, NULL)
  drop_blob <- serialize(drop, NULL)

  destroy_fmalloc_vector(drop, unsafe = TRUE)

  keep_recovered <- unserialize(keep_blob)
  drop_recover_error <- try(unserialize(drop_blob), silent = TRUE)

  list(
    keep_recovered_ok = all.equal(keep_recovered[], c(101L, 202L, 303L)) == TRUE,
    drop_recover_fails = inherits(drop_recover_error, "try-error")
  )
})
#> $keep_recovered_ok
#> [1] TRUE
#> 
#> $drop_recover_fails
#> [1] TRUE
```

## Runtime Modes

`open_fmalloc()` and `init_fmalloc()` accept `mode = "persistent"` or
`mode = "scratch"`. The default is `"persistent"`.

- In persistent mode, committed vector payloads are not returned to
  fmalloc by vector finalizers. Fixed-width atomic vectors serialize by
  reference to the physical allocation in the backing file.
- In scratch mode, the backing file is a temporary allocation arena.
  Vector finalizers may return payloads to fmalloc, and serialization
  falls back to ordinary R values.

``` r

local({
  persist_file <- tempfile(fileext = ".bin")
  rt <- open_fmalloc(persist_file, mode = "persistent")
  on.exit({
    cleanup_fmalloc(rt)
    unlink(persist_file)
  }, add = TRUE)

  x <- create_fmalloc_vector("integer", 5, runtime = rt)
  x[] <- 1:5
  roundtrip <- unserialize(serialize(x, NULL))
  roundtrip[]
})
#> [1] 1 2 3 4 5
```

## Inspecting fmalloc ALTREP Metadata

R’s internal inspector dispatches to the ALTREP `Inspect` hook.
Rfmalloc’s inspector reports the vector type, length, payload byte
count, runtime mode, runtime state, payload offset, file UUID, and
backing-file path.

``` r

local({
  inspect_file <- tempfile(fileext = ".bin")
  rt <- open_fmalloc(inspect_file, mode = "persistent")
  on.exit({
    cleanup_fmalloc(rt)
    unlink(inspect_file)
  }, add = TRUE)

  inspect_vec <- create_fmalloc_vector("integer", 4, runtime = rt)
  inspect_vec[] <- 1:4
  .Internal(inspect(inspect_vec))
})
#> @558744b01040 13 INTSXP g0c0 [OBJ,REF(1),ATT] fmalloc_altrep type=integer length=4 bytes=16 data=0x745e45a023e8 mode=persistent runtime=open offset=9192 uuid=68cb92d80f6d402d3bf719192ca84d29 file=/tmp/RtmpqvLn6G/file4c567640ed21b.bin
#> ATTRIB:
#>   @558744b03eb0 02 LISTSXP g0c0 [REF(1)] 
#>     TAG: @55873e330e40 01 SYMSXP g1c0 [MARK,REF(37243),LCK,gp=0x6000] "class" (has value)
#>     @558744adeba8 16 STRSXP g0c3 [REF(65535)] (len=3, tl=0)
#>       @5587436c02a8 09 CHARSXP g0c2 [REF(52),gp=0x60] [ASCII] [cached] "fmalloc_vector"
#>       @5587430e14e8 09 CHARSXP g0c1 [REF(169),gp=0x60] [ASCII] [cached] "fmalloc"
#>       @55873e363df8 09 CHARSXP g1c1 [MARK,REF(579),gp=0x61] [ASCII] [cached] "integer"
```

`inspect()` output is an internal R diagnostic, so exact formatting can
vary between R versions. The `fmalloc_altrep ...` line comes from
Rfmalloc’s ALTREP `Inspect` method.

## Character Vectors

Rfmalloc character vectors are ALTSTRING vectors. String bytes, lengths,
encodings, and NA flags live in fmalloc storage. R `CHARSXP` values are
materialized on `STRING_ELT()` access; Rfmalloc does not allocate
`CHARSXP` objects inside fmalloc.

``` r

local({
  char_file <- tempfile(fileext = ".bin")
  rt <- open_fmalloc(char_file, mode = "persistent")
  on.exit({
    cleanup_fmalloc(rt)
    unlink(char_file)
  }, add = TRUE)

  chars <- create_fmalloc_vector("character", 3, runtime = rt)
  chars[] <- c("one", NA_character_, "three")

  from_int <- create_fmalloc_vector("integer", 3, runtime = rt)
  from_int[] <- 1:3

  list(
    chars = chars[],
    from_integer = as.character(from_int)[]
  )
})
#> $chars
#> [1] "one"   NA      "three"
#> 
#> $from_integer
#> [1] "1" "2" "3"
```

## Matrix and data.frame constructors and converters

Use constructor helpers for shape-aware allocation, and `as_fmalloc_*()`
helpers to install shape metadata on existing fmalloc-backed vectors:

``` r

local({
  ctor_file <- tempfile(fileext = ".bin")
  rt <- open_fmalloc(ctor_file, mode = "persistent")
  on.exit({
    cleanup_fmalloc(rt)
    unlink(ctor_file)
  }, add = TRUE)

  m <- create_fmalloc_matrix("integer", nrow = 2L, ncol = 3L, runtime = rt)
  m[] <- 1:6

  base_vec <- create_fmalloc_vector("integer", 6L, runtime = rt)
  base_vec[] <- 11:16
  m_view <- as_fmalloc_matrix(base_vec, ncol = 3L, copy = FALSE)

  # Metadata-only reshape, then mutate through the reshaped object
  m_view[1L, 2L] <- 88L

  a <- create_fmalloc_array("numeric", dim = c(2L, 1L, 3L), runtime = rt)
  a[] <- 1:6

  col_a <- create_fmalloc_vector("integer", 3L, runtime = rt)
  col_b <- create_fmalloc_vector("integer", 3L, runtime = rt)
  col_a[] <- c(1L, 2L, 3L)
  col_b[] <- c(4L, 5L, 6L)
  df <- as_fmalloc_data_frame(a = col_a, b = col_b, stringsAsFactors = FALSE)

  list(
    matrix_dims = dim(m),
    metadata_shares_payload = c(base_vec[3L], m_view[1L, 2L]),
    array_dims = dim(a),
    df_columns = names(df)
  )
})
#> $matrix_dims
#> [1] 2 3
#> 
#> $metadata_shares_payload
#> [1] 13 88
#> 
#> $array_dims
#> [1] 2 1 3
#> 
#> $df_columns
#> [1] "a" "b"
```

## Reduction output materialization

`rowSums()`, `colSums()`, `rowMeans()`, `colMeans()`, and other
`Summary` pathways keep small results in regular R objects by default to
avoid unnecessary small temporary mappings. You can tune this behavior
with:

- `options(Rfmalloc.reduce_result_length = n)`

where `n` is the maximum allowed result length to keep in-memory.
Results with length greater than `n` stay fmalloc-backed.

``` r

local({
  reduce_file <- tempfile(fileext = ".bin")
  rt <- open_fmalloc(reduce_file, mode = "persistent")
  old_limit <- getOption("Rfmalloc.reduce_result_length")
  on.exit({
    cleanup_fmalloc(rt)
    options(Rfmalloc.reduce_result_length = old_limit)
    unlink(reduce_file)
  }, add = TRUE)

  m <- create_fmalloc_matrix("integer", nrow = 4L, ncol = 4L, runtime = rt)
  m[] <- 1:16

  base_m <- matrix(1:16, nrow = 4L, ncol = 4L)
  tiny_vec <- create_fmalloc_vector("integer", 4L, runtime = rt)
  tiny_vec[] <- 11:14

  options(Rfmalloc.reduce_result_length = 5L)
  default_row_sums <- rowSums(m)
  default_range <- range(tiny_vec)

  options(Rfmalloc.reduce_result_length = 3L)
  compact_row_sums <- rowSums(m)

  options(Rfmalloc.reduce_result_length = 1L)
  compact_range <- range(tiny_vec)

  list(
    default_row_sums_in_memory = inherits(default_row_sums, "fmalloc"),
    compact_row_sums_filebacked = inherits(compact_row_sums, "fmalloc"),
    default_range_in_memory = inherits(default_range, "fmalloc"),
    compact_range_filebacked = inherits(compact_range, "fmalloc"),
    rowSums_values = unclass(compact_row_sums),
    expected_rowSums = rowSums(base_m)
  )
})
#> $default_row_sums_in_memory
#> [1] FALSE
#> 
#> $compact_row_sums_filebacked
#> [1] TRUE
#> 
#> $default_range_in_memory
#> [1] FALSE
#> 
#> $compact_range_filebacked
#> [1] TRUE
#> 
#> $rowSums_values
#> [1] 28 32 36 40
#> 
#> $expected_rowSums
#> [1] 28 32 36 40
```

## Multiple Runtimes and Lifetime

Runtime handles make it possible to use more than one backing file in
one R process.

``` r

local({
  file_a <- tempfile(fileext = ".bin")
  file_b <- tempfile(fileext = ".bin")

  rt_a <- open_fmalloc(file_a)
  rt_b <- open_fmalloc(file_b, size_gb = 0.1)

  vec_a <- create_fmalloc_vector("integer", 10, runtime = rt_a)
  vec_b <- create_fmalloc_vector("numeric", 10, runtime = rt_b)
  vec_a[1] <- 101L
  vec_b[1] <- 202

  before_cleanup <- data.frame(
    vector = c("vec_a", "vec_b"),
    value = c(vec_a[1], vec_b[1])
  )

  cleanup_fmalloc(rt_a)
  after_cleanup <- vec_a[1]
  cleanup_fmalloc(rt_b)
  unlink(c(file_a, file_b))

  list(
    before_cleanup = before_cleanup,
    vec_a_after_cleanup = after_cleanup
  )
})
#> $before_cleanup
#>   vector value
#> 1  vec_a   101
#> 2  vec_b   202
#> 
#> $vec_a_after_cleanup
#> [1] 101
```

Calling `cleanup_fmalloc(rt_a)` marks `rt_a` closed. If vectors from
`rt_a` are still reachable, the native mapping is kept alive until those
vectors are garbage-collected. This is implemented with a native
live-vector count and an ALTREP-held external pointer for each
fmalloc-backed vector; no user-visible attribute is added to the vector.

## Serialization and Persistent References

Rfmalloc has an ALTREP serialization path for persistent runtimes. For
persistent fixed-width atomic and character vectors, `serialize()`
stores a small reference record instead of copying the vector payload
into the serialization stream. The record contains:

- format tag: `"RfmallocRef"`;
- reference format version;
- backing-file path;
- backing-file UUID;
- R vector type;
- vector length;
- payload offset inside the mapped file;
- payload byte size.

During `unserialize()`, Rfmalloc reopens the backing file, verifies the
UUID, checks the recorded dimensions and file bounds, validates the
catalog record and generation, and reconstructs an ALTREP vector around
the same fmalloc allocation.

``` r

local({
  ser_file <- tempfile(fileext = ".bin")
  rt <- open_fmalloc(ser_file, mode = "persistent")

  ints <- create_fmalloc_vector("integer", 4, runtime = rt)
  ints[] <- 101:104
  chars <- create_fmalloc_vector("character", 3, runtime = rt)
  chars[] <- c("alpha", NA_character_, "gamma")

  catalog <- list_fmalloc_allocations(rt)
  ints_blob <- serialize(ints, NULL)
  chars_blob <- serialize(chars, NULL)

  cleanup_fmalloc(rt)

  ints_recovered <- unserialize(ints_blob)
  chars_recovered <- unserialize(chars_blob)
  output <- list(
    catalog = catalog[, c("record_offset", "generation", "type", "length")],
    integers = ints_recovered[],
    characters = chars_recovered[]
  )

  unlink(ser_file)
  output
})
#> $catalog
#>   record_offset generation      type length
#> 1          9440          2 character      3
#> 2          9224          1   integer      4
#> 
#> $integers
#> [1] 101 102 103 104
#> 
#> $characters
#> [1] "alpha" NA      "gamma"
```

## Catalog diagnostics

Use `diagnose_fmalloc_runtime()` to inspect runtime metadata, the live
catalog, and a compact summary that can help decide when a runtime is a
reset candidate:

``` r
local({
  diag_file <- tempfile(fileext = ".bin")
  rt <- open_fmalloc(diag_file, mode = "persistent")

  a <- create_fmalloc_vector("integer", 4, runtime = rt)
  b <- create_fmalloc_vector("character", 2, runtime = rt)
  a[] <- 1:4
  b[] <- c("left", "right")

  pre <- diagnose_fmalloc_runtime(rt)
  destroy_fmalloc_vector(a)
  post <- diagnose_fmalloc_runtime(rt)

  list(
    mode = post$runtime$mode,
    pre_records = pre$summary$record_count,
    pre_committed = pre$summary$committed_records,
    post_records = post$summary$record_count,
    post_tombstones = post$summary$tombstoned_records,
    compaction_implemented = post$summary$compaction_implemented
  )
})
#> $mode
#> [1] "persistent"
#> 
#> $pre_records
#> [1] 2
#> 
#> $pre_committed
#> [1] 2
#> 
#> $post_records
#> [1] 2
#> 
#> $post_tombstones
#> [1] 1
#> 
#> $compaction_implemented
#> [1] FALSE
```

Scratch runtimes use ordinary R serialization instead. Their serialized
values do not depend on reopening the scratch backing file.

``` r
local({
  scratch_file <- tempfile(fileext = ".bin")
  scratch_rt <- open_fmalloc(scratch_file, mode = "scratch")

  scratch_vec <- create_fmalloc_vector("integer", 4, runtime = scratch_rt)
  scratch_vec[] <- 1:4
  scratch_copy <- unserialize(serialize(scratch_vec, NULL))

  cleanup_fmalloc(scratch_rt)
  unlink(scratch_file)
  scratch_copy
})
#> [1] 1 2 3 4
#> attr(,"class")
#> [1] "fmalloc_vector" "fmalloc"        "integer"
```

Rfmalloc list vectors are intentionally not general R lists. Element
assignment accepts only `NULL` or Rfmalloc-backed vectors from the same
runtime; ordinary R objects such as base vectors, data frames, or
arbitrary lists are rejected. This keeps lists inside the same
file-backed object universe.

For persistent runtimes, list containers now serialize as reference
state when all children are recoverable fmalloc vectors. The serialized
state stores per-slot child descriptors, so nested list containers can
reopen recursively from the same backing file without carrying
session-local `SEXP` pointers.

This is reference-based recovery, not name-based object discovery. The
catalog stores physical allocation records, not user variable names.
Serialized references use the catalog record offset and generation for
validation; the catalog can be listed, but it is not yet a high-level
object store for recovering vectors by name.

## List constraints and recovery examples

List assignment rejects non-fmalloc payloads at runtime.

``` r

local({
  reject_file <- tempfile(fileext = ".bin")
  rt <- open_fmalloc(reject_file, mode = "persistent")
  on.exit({
    cleanup_fmalloc(rt)
    unlink(reject_file)
  }, add = TRUE)

  fm_list <- create_fmalloc_vector("list", 2, runtime = rt)
  child <- create_fmalloc_vector("integer", 2, runtime = rt)
  child[] <- c(1L, 2L)
  fm_list[[1]] <- child

  bad_assignment <- try({
    fm_list[[2]] <- 1:2
  }, silent = TRUE)

  list(
    same_runtime_child = fm_list[[1]][],
    rejected_non_fmalloc_payload = inherits(bad_assignment, "try-error")
  )
})
#> $same_runtime_child
#> [1] 1 2
#> 
#> $rejected_non_fmalloc_payload
#> [1] TRUE
```

Nested fmalloc lists recover recursively when serialized and
unserialized from a persistent backing file.

``` r

local({
  recover_file <- tempfile(fileext = ".bin")
  rt <- open_fmalloc(recover_file, mode = "persistent")
  on.exit(unlink(recover_file), add = TRUE)

  child <- create_fmalloc_vector("integer", 2, runtime = rt)
  child[] <- 1:2

  nested <- create_fmalloc_vector("list", 1, runtime = rt)
  nested[[1]] <- child

  labels <- create_fmalloc_vector("character", 2, runtime = rt)
  labels[] <- c("alpha", "beta")

  fm_list <- create_fmalloc_vector("list", 2, runtime = rt)
  fm_list[[1]] <- nested
  fm_list[[2]] <- labels

  blob <- serialize(fm_list, NULL)

  cleanup_fmalloc(rt)

  recovered <- unserialize(blob)
  list(
    recovered_nested = recovered[[1]][[1]][],
    recovered_labels = recovered[[2]]
  )
})
#> $recovered_nested
#> [1] 1 2
#> 
#> $recovered_labels
#> [1] "alpha" "beta"
```

Cross-runtime insertion is also rejected.

``` r

local({
  cross_file_a <- tempfile(fileext = ".bin")
  cross_file_b <- tempfile(fileext = ".bin")

  rt_a <- open_fmalloc(cross_file_a, mode = "persistent")
  rt_b <- open_fmalloc(cross_file_b, mode = "persistent")
  on.exit({
    cleanup_fmalloc(rt_a)
    cleanup_fmalloc(rt_b)
    unlink(c(cross_file_a, cross_file_b))
  }, add = TRUE)

  child_other_runtime <- create_fmalloc_vector("integer", 2, runtime = rt_b)
  child_other_runtime[] <- c(10L, 20L)

  fm_list <- create_fmalloc_vector("list", 1, runtime = rt_a)
  cross_runtime_error <- try({
    fm_list[[1]] <- child_other_runtime
  }, silent = TRUE)

  list(cross_runtime_rejected = inherits(cross_runtime_error, "try-error"))
})
#> $cross_runtime_rejected
#> [1] TRUE
```

## Performance

Rfmalloc vectors are regular R vectors from R’s point of view, but
different operations exercise different paths through R and ALTREP:

- vectorized sequential operations such as `sum(x)` can use contiguous
  payload access;
- scalar loops use ALTREP element access and are more call-heavy;
- `x[i]` creates a fmalloc-backed subset copy for supported vector
  types;
- `bench::mark()` reports R heap allocation in `mem_alloc`; it does not
  count bytes stored in the fmalloc mapped file.

The small benchmark below uses `bench` and a scratch runtime, so
benchmark-only fmalloc allocations are not preserved as persistent
records. Timings are machine- and R-version-specific; use this as a
template for local measurements.

``` r

perf_file <- tempfile(fileext = ".bin")
rt <- open_fmalloc(perf_file, mode = "scratch", size_gb = 0.1)

n <- 100000L
set.seed(1)
idx <- sample.int(n, 2000L)

base_int <- integer(n)
base_int[] <- seq_len(n)

fm_int <- create_fmalloc_vector("integer", n, runtime = rt)
fm_int[] <- base_int

write_env <- new.env(parent = emptyenv())
write_env$base <- base_int
write_env$fm <- create_fmalloc_vector("integer", n, runtime = rt)
write_env$fm[] <- base_int

scalar_sum <- function(x, i) {
  total <- 0L
  for (j in i) total <- total + x[[j]]
  total
}

perf_result <- bench::mark(
  base_sequential_sum = sum(base_int),
  fmalloc_sequential_sum = sum(fm_int),
  base_scalar_read = scalar_sum(base_int, idx),
  fmalloc_scalar_read = scalar_sum(fm_int, idx),
  base_subset_copy = base_int[idx],
  fmalloc_subset_copy = fm_int[idx],
  base_indexed_write = {
    write_env$base[idx] <- 0L
    invisible(write_env$base[1L])
  },
  fmalloc_indexed_write = {
    write_env$fm[idx] <- 0L
    invisible(write_env$fm[1L])
  },
  iterations = 20,
  check = FALSE
)[, c("expression", "median", "itr/sec", "mem_alloc", "gc/sec")]

rm(fm_int, write_env)
invisible(gc())
cleanup_fmalloc(rt)
unlink(perf_file)
perf_result
#> # A tibble: 8 × 5
#>   expression               median `itr/sec` mem_alloc `gc/sec`
#>   <bch:expr>             <bch:tm>     <dbl> <bch:byt>    <dbl>
#> 1 base_sequential_sum     36.85µs    25326.        0B        0
#> 2 fmalloc_sequential_sum  38.36µs    23820.        0B        0
#> 3 base_scalar_read        32.67µs    28076.        0B        0
#> 4 fmalloc_scalar_read    920.43µs     1079.   24.55KB        0
#> 5 base_subset_copy         3.02µs   274523.    7.86KB        0
#> 6 fmalloc_subset_copy     31.48µs    31095.        0B        0
#> 7 base_indexed_write     113.27µs    11760.  390.67KB        0
#> 8 fmalloc_indexed_write  199.97µs     5277.        0B        0
```

## Native C API for Other Packages

Rfmalloc installs `inst/include/Rfmalloc.h` and registers C-callable
entry points with `R_RegisterCCallable()`. Downstream packages can add
Rfmalloc to `LinkingTo` and `Imports`, include the header, and use the
inline wrappers.

The current native surface exposes runtime open/cleanup, vector
creation, default-runtime lookup, catalog listing, runtime diagnostics,
and an API-version query. Returned `SEXP` objects follow normal R API
ownership rules.

## References

- fmalloc: <https://github.com/yasukata/fmalloc>
- Simon Urbanek’s `Rf_allocVector3()` custom mmap allocator PoC:
  <https://gist.github.com/s-u/6712c97ca74181f5a1a5>
- Rfmalloc’s prior custom allocator implementation using
  `Rf_allocVector3()` (for historical comparison):
  <https://github.com/sounkou-bioinfo/Rfmalloc/commit/0165953>

## License

GPL (\>= 2)
