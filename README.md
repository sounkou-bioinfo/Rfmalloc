
# Rfmalloc

<!-- badges: start -->
[![R-CMD-check](https://github.com/sounkou-bioinfo/Rfmalloc/actions/workflows/R-CMD-check.yaml/badge.svg)](https://github.com/sounkou-bioinfo/Rfmalloc/actions/workflows/R-CMD-check.yaml)
<!-- badges: end -->

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
  complex, character, and list values;
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
tooling, persistent list recovery semantics, and mapped-file recovery
diagnostics.

## Installation

``` r
# install.packages("devtools")
devtools::install_github("sounkou-bioinfo/Rfmalloc")
```

## Basic Usage

The examples use `local()` and `on.exit()` only to keep temporary
backing files scoped while the README is rendered.

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
  v_lst[[1]] <- data.frame(x = 1:2)

  list(
    integer = v_int[1:3],
    numeric = v_num[1:3],
    raw = v_raw[],
    complex = v_cplx[],
    character = v_chr[],
    list_first = v_lst[[1]]
  )
})
#> Creating file with size: 33562624 bytes (0.03 GB)
#> fmalloc initialized with file: /tmp/RtmpuijWbZ/file7b34a29f6510d.bin (init: true, mode: persistent)
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
#>   x
#> 1 1
#> 2 2
```

For multiple backing files or explicit lifetime management, prefer
runtime handles returned by `open_fmalloc()`:

``` r
library(Rfmalloc)

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
#> fmalloc initialized with file: /tmp/RtmpuijWbZ/file7b34a69783054.bin (init: true, mode: persistent)
#> Cleaning up fmalloc...
#> fmalloc cleaned up
#> [1] 10 11 12
```

## Larger Allocation Example

``` r
library(Rfmalloc)

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
#> fmalloc initialized with file: /tmp/RtmpuijWbZ/file7b34a22392348.bin (init: true, mode: persistent)
#> Creating fmalloc ALTREP vector: type=integer, length=1000000000
#> Large allocation: 3814.70 MB requested
#> SUCCESS: fmalloc allocated 4000000000 bytes
#> Successfully created fmalloc ALTREP vector
#> Cleaning up fmalloc...
#> fmalloc cleaned up
#> [1] 1 2 3 4 5
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
library(Rfmalloc)

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
#> fmalloc initialized with file: /tmp/RtmpuijWbZ/file7b34a1f6dbdc1.bin (init: true, mode: persistent)
#> Using existing file: /tmp/RtmpuijWbZ/file7b34a1f6dbdc1.bin (size: 33562624 bytes)
#> fmalloc initialized with file: /tmp/RtmpuijWbZ/file7b34a1f6dbdc1.bin (init: false, mode: persistent)
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
library(Rfmalloc)

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
#> fmalloc initialized with file: /tmp/RtmpuijWbZ/file7b34a569dbf7b.bin (init: true, mode: persistent)
#> @5862924f05b8 13 INTSXP g0c0 [REF(1)] fmalloc_altrep type=integer length=4 bytes=16 data=0x7b3bac2023e8 mode=persistent runtime=open offset=9192 uuid=b3a34f02d203379df41c6918d9c79b80 file=/tmp/RtmpuijWbZ/file7b34a569dbf7b.bin
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
library(Rfmalloc)

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
#> fmalloc initialized with file: /tmp/RtmpuijWbZ/file7b34a4c6f234e.bin (init: true, mode: persistent)
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
library(Rfmalloc)

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
#> fmalloc initialized with file: /tmp/RtmpuijWbZ/file7b34a39a21c1a.bin (init: true, mode: persistent)
#> Requested file size: 0.10 GB (107374182 bytes)
#> Creating file with size: 107374182 bytes (0.10 GB)
#> fmalloc initialized with file: /tmp/RtmpuijWbZ/file7b34a7c48226f.bin (init: true, mode: persistent)
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
library(Rfmalloc)

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
#> fmalloc initialized with file: /tmp/RtmpuijWbZ/file7b34a6b32ad90.bin (init: true, mode: persistent)
#> Cleaning up fmalloc...
#> fmalloc cleaned up
#> Using existing file: /tmp/RtmpuijWbZ/file7b34a6b32ad90.bin (size: 33562624 bytes)
#> fmalloc initialized with file: /tmp/RtmpuijWbZ/file7b34a6b32ad90.bin (init: false, mode: persistent)
#> Using existing file: /tmp/RtmpuijWbZ/file7b34a6b32ad90.bin (size: 33562624 bytes)
#> fmalloc initialized with file: /tmp/RtmpuijWbZ/file7b34a6b32ad90.bin (init: false, mode: persistent)
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
#> fmalloc initialized with file: /tmp/RtmpuijWbZ/file7b34a78731d72.bin (init: true, mode: scratch)
#> Cleaning up fmalloc...
#> fmalloc cleaned up
#> [1] 1 2 3 4
```

List vectors are serialized as ordinary R list values for now.
Persistent list recovery needs a separate element-level policy because
list elements are R objects, not raw fixed-width payload bytes.

This is reference-based recovery, not name-based object discovery. The
catalog stores physical allocation records, not user variable names.
Serialized references use the catalog record offset and generation for
validation; the catalog can be listed, but it is not yet a high-level
object store for recovering vectors by name.

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
