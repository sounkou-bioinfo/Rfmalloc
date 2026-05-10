# Rfmalloc

Rfmalloc is an experimental R package for file-backed R vectors using a
patched copy of [fmalloc](https://github.com/yasukata/fmalloc). The
current R-facing vector API creates fmalloc-backed ALTREP vectors for
atomic, character, and list storage.

Earlier prototypes explored R’s custom allocator / `Rf_allocVector3()`
path. The package has moved to direct ALTREP vectors for the public
vector API. The patched fmalloc `malloc`/`free`/`realloc` layer is still
built and installed for native consumers that use the C API or link
against the bundled library.

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
- an in-file persistent allocation catalog used to validate serialized
  references;
- an installed C header and `R_RegisterCCallable()` API for other
  packages.

Still experimental: view-based subsets, richer catalog maintenance
tooling, list-container recovery by reference, and mapped-file recovery
diagnostics.

## Installation

``` r

# install.packages("devtools")
devtools::install_github("sounkou-bioinfo/Rfmalloc")
```

## Basic Usage

The examples use temporary backing files. Several chunks wrap code in
[`local()`](https://rdrr.io/r/base/eval.html) only so
[`on.exit()`](https://rdrr.io/r/base/on.exit.html) can clean those files
up while the README is rendered; this is not required in normal
interactive use. In your own code, keep the runtime handle you need and
call
[`cleanup_fmalloc()`](https://sounkou-bioinfo.github.io/Rfmalloc/reference/cleanup_fmalloc.md)
when finished.

The shortest form uses
[`init_fmalloc()`](https://sounkou-bioinfo.github.io/Rfmalloc/reference/init_fmalloc.md),
which opens a backing file and stores it as the package default runtime.
Calls to
[`create_fmalloc_vector()`](https://sounkou-bioinfo.github.io/Rfmalloc/reference/create_fmalloc_vector.md)
can then omit the `runtime` argument:

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
#> Creating file with size: 33562624 bytes (0.03 GB)
#> fmalloc initialized with file: /tmp/RtmpNOppVR/file123b2e513c4204.bin (init: true, mode: persistent)
#> Cleaning up fmalloc...
#> fmalloc cleaned up
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
runtime handles returned by
[`open_fmalloc()`](https://sounkou-bioinfo.github.io/Rfmalloc/reference/open_fmalloc.md):

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
#> Creating file with size: 33562624 bytes (0.03 GB)
#> fmalloc initialized with file: /tmp/RtmpNOppVR/file123b2e565e1683.bin (init: true, mode: persistent)
#> Cleaning up fmalloc...
#> fmalloc cleaned up
#> [1] 10 11 12
```

## Larger Allocation Example

``` r


local({
  large_file <- tempfile(fileext = ".bin")
  init_fmalloc(large_file, size_gb = 5)
  on.exit({
    cleanup_fmalloc()
    unlink(large_file)
  }, add = TRUE)

  big_int <- local({
    x <- create_fmalloc_vector("integer", 1e9) # about 4 GB of payload
    x[1:5] <- 1:5
    x
  })
  big_int[1:5]
})
#> Requested file size: 5.00 GB (5368709120 bytes)
#> Creating file with size: 5368709120 bytes (5.00 GB)
#> fmalloc initialized with file: /tmp/RtmpNOppVR/file123b2e7d0ab6aa.bin (init: true, mode: persistent)
#> Creating fmalloc ALTREP vector: type=integer, length=1000000000
#> Large allocation: 3814.70 MB requested
#> SUCCESS: fmalloc allocated 4000000000 bytes
#> Successfully created fmalloc ALTREP vector
#> Cleaning up fmalloc...
#> fmalloc cleaned up
#> [1] 1 2 3 4 5
```

## Runtime Modes

[`open_fmalloc()`](https://sounkou-bioinfo.github.io/Rfmalloc/reference/open_fmalloc.md)
and
[`init_fmalloc()`](https://sounkou-bioinfo.github.io/Rfmalloc/reference/init_fmalloc.md)
accept `mode = "persistent"` or `mode = "scratch"`. The default is
`"persistent"`.

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
#> Creating file with size: 33562624 bytes (0.03 GB)
#> fmalloc initialized with file: /tmp/RtmpNOppVR/file123b2e1fb2ca09.bin (init: true, mode: persistent)
#> Using existing file: /tmp/RtmpNOppVR/file123b2e1fb2ca09.bin (size: 33562624 bytes)
#> fmalloc initialized with file: /tmp/RtmpNOppVR/file123b2e1fb2ca09.bin (init: false, mode: persistent)
#> Cleaning up fmalloc...
#> fmalloc cleaned up
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
#> Creating file with size: 33562624 bytes (0.03 GB)
#> fmalloc initialized with file: /tmp/RtmpNOppVR/file123b2e60bae0c3.bin (init: true, mode: persistent)
#> @5ef41e086958 13 INTSXP g0c0 [REF(1)] fmalloc_altrep type=integer length=4 bytes=16 data=0x6ffbcbc023e8 mode=persistent runtime=open offset=9192 uuid=b52a667e79c9fc7287980cc0db0363f3 file=/tmp/RtmpNOppVR/file123b2e60bae0c3.bin
#> Cleaning up fmalloc...
#> fmalloc cleaned up
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
#> Creating file with size: 33562624 bytes (0.03 GB)
#> fmalloc initialized with file: /tmp/RtmpNOppVR/file123b2e4817caa2.bin (init: true, mode: persistent)
#> Cleaning up fmalloc...
#> fmalloc cleaned up
#> $chars
#> [1] "one"   NA      "three"
#> 
#> $from_integer
#> [1] "1" "2" "3"
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
#> Creating file with size: 33562624 bytes (0.03 GB)
#> fmalloc initialized with file: /tmp/RtmpNOppVR/file123b2e24c77b88.bin (init: true, mode: persistent)
#> Requested file size: 0.10 GB (107374182 bytes)
#> Creating file with size: 107374182 bytes (0.10 GB)
#> fmalloc initialized with file: /tmp/RtmpNOppVR/file123b2e8fb4de4.bin (init: true, mode: persistent)
#> Cleaning up fmalloc...
#> fmalloc cleaned up
#> Cleaning up fmalloc...
#> fmalloc cleaned up
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
persistent fixed-width atomic and character vectors,
[`serialize()`](https://rdrr.io/r/base/serialize.html) stores a small
reference record instead of copying the vector payload into the
serialization stream. The record contains:

- format tag: `"RfmallocRef"`;
- reference format version;
- backing-file path;
- backing-file UUID;
- R vector type;
- vector length;
- payload offset inside the mapped file;
- payload byte size.

During [`unserialize()`](https://rdrr.io/r/base/serialize.html),
Rfmalloc reopens the backing file, verifies the UUID, checks the
recorded dimensions and file bounds, validates the catalog record and
generation, and reconstructs an ALTREP vector around the same fmalloc
allocation.

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
#> Creating file with size: 33562624 bytes (0.03 GB)
#> fmalloc initialized with file: /tmp/RtmpNOppVR/file123b2e23c79fde.bin (init: true, mode: persistent)
#> Cleaning up fmalloc...
#> fmalloc cleaned up
#> Using existing file: /tmp/RtmpNOppVR/file123b2e23c79fde.bin (size: 33562624 bytes)
#> fmalloc initialized with file: /tmp/RtmpNOppVR/file123b2e23c79fde.bin (init: false, mode: persistent)
#> Using existing file: /tmp/RtmpNOppVR/file123b2e23c79fde.bin (size: 33562624 bytes)
#> fmalloc initialized with file: /tmp/RtmpNOppVR/file123b2e23c79fde.bin (init: false, mode: persistent)
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
#> Creating file with size: 33562624 bytes (0.03 GB)
#> fmalloc initialized with file: /tmp/RtmpNOppVR/file123b2e250d1e30.bin (init: true, mode: scratch)
#> Cleaning up fmalloc...
#> fmalloc cleaned up
#> [1] 1 2 3 4
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
#> Creating file with size: 33562624 bytes (0.03 GB)
#> fmalloc initialized with file: /tmp/RtmpNOppVR/file123b2e9dc417b.bin (init: true, mode: persistent)
#> Cleaning up fmalloc...
#> fmalloc cleaned up
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
#> Creating file with size: 33562624 bytes (0.03 GB)
#> fmalloc initialized with file: /tmp/RtmpNOppVR/file123b2e5b18837f.bin (init: true, mode: persistent)
#> Cleaning up fmalloc...
#> fmalloc cleaned up
#> Using existing file: /tmp/RtmpNOppVR/file123b2e5b18837f.bin (size: 33562624 bytes)
#> fmalloc initialized with file: /tmp/RtmpNOppVR/file123b2e5b18837f.bin (init: false, mode: persistent)
#> Using existing file: /tmp/RtmpNOppVR/file123b2e5b18837f.bin (size: 33562624 bytes)
#> fmalloc initialized with file: /tmp/RtmpNOppVR/file123b2e5b18837f.bin (init: false, mode: persistent)
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
#> Creating file with size: 33562624 bytes (0.03 GB)
#> fmalloc initialized with file: /tmp/RtmpNOppVR/file123b2e31769a03.bin (init: true, mode: persistent)
#> Creating file with size: 33562624 bytes (0.03 GB)
#> fmalloc initialized with file: /tmp/RtmpNOppVR/file123b2e2ba2e00b.bin (init: true, mode: persistent)
#> Cleaning up fmalloc...
#> fmalloc cleaned up
#> Cleaning up fmalloc...
#> fmalloc cleaned up
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
- [`bench::mark()`](https://bench.r-lib.org/reference/mark.html) reports
  R heap allocation in `mem_alloc`; it does not count bytes stored in
  the fmalloc mapped file.

The small benchmark below uses `bench` and a scratch runtime, so
benchmark-only fmalloc allocations are not preserved as persistent
records. Timings are machine- and R-version-specific; use this as a
template for local measurements.

``` r


perf_file <- tempfile(fileext = ".bin")
rt <- open_fmalloc(perf_file, mode = "scratch", size_gb = 0.1)
#> Requested file size: 0.10 GB (107374182 bytes)
#> Creating file with size: 107374182 bytes (0.10 GB)
#> fmalloc initialized with file: /tmp/RtmpNOppVR/file123b2e68e14464.bin (init: true, mode: scratch)

n <- 100000L
set.seed(1)
idx <- sample.int(n, 2000L)

base_int <- integer(n)
base_int[] <- seq_len(n)

fm_int <- create_fmalloc_vector("integer", n, runtime = rt)
#> Creating fmalloc ALTREP vector: type=integer, length=100000
#> Successfully created fmalloc ALTREP vector
fm_int[] <- base_int

write_env <- new.env(parent = emptyenv())
write_env$base <- base_int
write_env$fm <- create_fmalloc_vector("integer", n, runtime = rt)
#> Creating fmalloc ALTREP vector: type=integer, length=100000
#> Successfully created fmalloc ALTREP vector
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
#> Cleaning up fmalloc...
#> fmalloc cleaned up
unlink(perf_file)
perf_result
#> # A tibble: 8 × 5
#>   expression               median `itr/sec` mem_alloc `gc/sec`
#>   <bch:expr>             <bch:tm>     <dbl> <bch:byt>    <dbl>
#> 1 base_sequential_sum     33.57µs    28355.        0B        0
#> 2 fmalloc_sequential_sum   33.6µs    28455.        0B        0
#> 3 base_scalar_read        32.14µs    29032.   24.55KB        0
#> 4 fmalloc_scalar_read     61.35µs    16235.        0B        0
#> 5 base_subset_copy         2.73µs   259672.    7.86KB        0
#> 6 fmalloc_subset_copy      9.98µs    79272.        0B        0
#> 7 base_indexed_write     107.84µs     8466.  390.67KB        0
#> 8 fmalloc_indexed_write  182.62µs     5264.        0B        0
```

## Native C API for Other Packages

Rfmalloc installs `inst/include/Rfmalloc.h` and registers C-callable
entry points with `R_RegisterCCallable()`. Downstream packages can add
Rfmalloc to `LinkingTo` and `Imports`, include the header, and use the
inline wrappers.

The current native surface exposes runtime open/cleanup, vector
creation, default-runtime lookup, catalog listing, and an API-version
query. Returned `SEXP` objects follow normal R API ownership rules.

## References

- fmalloc: <https://github.com/yasukata/fmalloc>

## License

GPL (\>= 2)
