
# Rfmalloc

Rfmalloc is an experimental R package for allocating vector storage from
a memory-mapped backing file using a patched copy of
[fmalloc](https://github.com/yasukata/fmalloc). The goal is to explore
large, file-backed R vector storage without routing large allocations
back through the process heap.

The current package exposes ALTREP vectors whose fixed-width atomic
payloads and character-vector string storage are allocated directly from
fmalloc. This uses the ALTREP C entry points available in R’s installed
headers instead of relying on the non-API `Rf_allocVector3()` vector
allocator hook.

## Current Status

Recent allocator work fixed the stress-path that previously fell back
into ptmalloc/dlmalloc’s system `mmap()` path for large requests. Large
allocations are now carved from contiguous runs in the fmalloc backing
file instead.

Implemented now:

- ALTREP file-backed allocation for logical, integer, numeric, raw,
  complex, character, and list vectors;
- large allocations spanning multiple fmalloc chunks;
- `malloc`, `free`, and `realloc` support through the patched fmalloc
  layer;
- reopening backing files created by the current allocator format;
- explicit runtime handles, so multiple fmalloc runtimes can be open in
  one R process;
- explicit runtime modes: `persistent` keeps committed vector payloads
  in the file, while `scratch` returns payloads to fmalloc when ALTREP
  handles are garbage-collected;
- persistent ALTREP serialization for fixed-width atomic and character
  vectors using physical allocation metadata: path, file UUID, type,
  length, payload offset, and byte size;
- ALTREP `Coerce` for supported fmalloc-backed target types, so
  coercions such as `as.numeric(x)` and `as.character(x)` allocate their
  result in the same fmalloc runtime;
- ALTREP `Extract_subset` for vector indexing operations such as `x[i]`,
  returning fmalloc-backed copy results;
- an in-file persistent allocation catalog with record offsets,
  generations, types, lengths, payload offsets, byte sizes, states, and
  flags;
- native lifetime tracking from ALTREP vector handles to runtime
  mappings, so a runtime mapping is not destroyed while vectors
  allocated from it are still reachable.

Still experimental / future work:

- view-based ALTREP subsets for simple contiguous or strided indexing
  patterns;
- richer catalog tooling, including compaction/reset and stale-record
  diagnostics;
- richer persistence semantics for pointer-containing R types; list
  vectors are session-local containers, although their fmalloc-vector
  elements can serialize through their own persistent references;
- stronger mapped-file lifecycle behavior: clearer close semantics,
  recovery from interrupted sessions, stale backing-file diagnostics,
  and platform-specific mapping edge cases.

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
#> fmalloc initialized with file: /tmp/RtmpQlStDo/file72e7b5fd5a24e.bin (init: true, mode: persistent)
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
#> fmalloc initialized with file: /tmp/RtmpQlStDo/file72e7b74aa9ad2.bin (init: true, mode: persistent)
#> Cleaning up fmalloc...
#> fmalloc cleaned up
#> [1] 10 11 12
```

## Larger Allocation Example

This is the kind of stress case the package is meant for: it creates a 5
GB backing file and allocates about 4 GB of integer payload.

``` r
library(Rfmalloc)

local({
  large_file <- tempfile(fileext = ".bin")
  init_fmalloc(large_file, size_gb = 5)
  on.exit({
    cleanup_fmalloc()
    unlink(large_file)
  }, add = TRUE)

  # 1 billion integers = about 4 GB of payload, backed by fmalloc.
  # Keep creation and initialization in one local expression so README rendering
  # does not keep an extra transient reference that would force a second 4 GB COW
  # duplicate during `[<-`.
  big_int <- local({
    x <- create_fmalloc_vector("integer", 1e9)
    x[1:5] <- 1:5
    x
  })
  big_int[1:5]
})
#> Requested file size: 5.00 GB (5368709120 bytes)
#> Creating file with size: 5368709120 bytes (5.00 GB)
#> fmalloc initialized with file: /tmp/RtmpQlStDo/file72e7b1a0fee99.bin (init: true, mode: persistent)
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
#> fmalloc initialized with file: /tmp/RtmpQlStDo/file72e7b3803f31.bin (init: true, mode: persistent)
#> Using existing file: /tmp/RtmpQlStDo/file72e7b3803f31.bin (size: 33562624 bytes)
#> fmalloc initialized with file: /tmp/RtmpQlStDo/file72e7b3803f31.bin (init: false, mode: persistent)
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
#> fmalloc initialized with file: /tmp/RtmpQlStDo/file72e7b66c8e821.bin (init: true, mode: persistent)
#> @576b3345e598 13 INTSXP g0c0 [REF(1)] fmalloc_altrep type=integer length=4 bytes=16 data=0x763d886023e8 mode=persistent runtime=open offset=9192 uuid=7f6dc67e0e0b9955e452230a99fe5fb4 file=/tmp/RtmpQlStDo/file72e7b66c8e821.bin
#> Cleaning up fmalloc...
#> fmalloc cleaned up
```

`inspect()` output is an internal R diagnostic, so exact formatting can
vary between R versions. The `fmalloc_altrep ...` line comes from
Rfmalloc’s ALTREP `Inspect` method.

## Character Vectors

Rfmalloc character vectors are ALTSTRING vectors. They do not allocate R
`CHARSXP` objects inside fmalloc. R’s current sources keep `CHARSXP`
allocation on internal string-cache paths, and `allocVector3()` is
explicitly not a public API. Instead, Rfmalloc stores string bytes,
lengths, encodings, and NA flags in fmalloc allocations. `STRING_ELT()`
materializes an ordinary R `CHARSXP` with `mkCharLenCE()` when R asks
for an element.

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
#> fmalloc initialized with file: /tmp/RtmpQlStDo/file72e7b5c5a0941.bin (init: true, mode: persistent)
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
the same R process. Allocator entry is serialized with native mutexes
because the current fmalloc/ptmalloc layer still has process-global
target state internally.

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
#> fmalloc initialized with file: /tmp/RtmpQlStDo/file72e7b3379f3f.bin (init: true, mode: persistent)
#> Requested file size: 0.10 GB (107374182 bytes)
#> Creating file with size: 107374182 bytes (0.10 GB)
#> fmalloc initialized with file: /tmp/RtmpQlStDo/file72e7b75f438b0.bin (init: true, mode: persistent)
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
#> fmalloc initialized with file: /tmp/RtmpQlStDo/file72e7b2e0e1b90.bin (init: true, mode: persistent)
#> Cleaning up fmalloc...
#> fmalloc cleaned up
#> Using existing file: /tmp/RtmpQlStDo/file72e7b2e0e1b90.bin (size: 33562624 bytes)
#> fmalloc initialized with file: /tmp/RtmpQlStDo/file72e7b2e0e1b90.bin (init: false, mode: persistent)
#> Using existing file: /tmp/RtmpQlStDo/file72e7b2e0e1b90.bin (size: 33562624 bytes)
#> fmalloc initialized with file: /tmp/RtmpQlStDo/file72e7b2e0e1b90.bin (init: false, mode: persistent)
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
#> fmalloc initialized with file: /tmp/RtmpQlStDo/file72e7b36e8d1bc.bin (init: true, mode: scratch)
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

## Watching ALTREP-Controlled Duplication with `lobstr`

`lobstr::obj_addr()` is useful for seeing why the vectors are ALTREP
from the beginning. R’s copy-on-write path dispatches to the ALTREP
`Duplicate` method, and the duplicate allocates its payload from
fmalloc.

``` r
library(Rfmalloc)
library(lobstr)

local({
  addr_file <- tempfile(fileext = ".bin")
  init_fmalloc(addr_file)
  on.exit({
    cleanup_fmalloc()
    unlink(addr_file)
  }, add = TRUE)

  cow_a <- create_fmalloc_vector("integer", 10)
  cow_a[1:3] <- 1:3
  cow_b <- cow_a

  before <- data.frame(
    object = c("cow_a", "cow_b"),
    address = c(obj_addr(cow_a), obj_addr(cow_b))
  )

  cow_a[1] <- 99L

  after <- data.frame(
    object = c("cow_a", "cow_b"),
    address = c(obj_addr(cow_a), obj_addr(cow_b))
  )

  list(before = before, after = after)
})
#> Creating file with size: 33562624 bytes (0.03 GB)
#> fmalloc initialized with file: /tmp/RtmpQlStDo/file72e7b76833136.bin (init: true, mode: persistent)
#> Cleaning up fmalloc...
#> fmalloc cleaned up
#> $before
#>   object        address
#> 1  cow_a 0x576b30f21fa8
#> 2  cow_b 0x576b30f21fa8
#> 
#> $after
#>   object        address
#> 1  cow_a 0x576b30efe3b0
#> 2  cow_b 0x576b30f21fa8
```

The two names start as references to the same `SEXP`. After the write to
`cow_a`, `lobstr::obj_addr()` shows that `cow_a` moved to a new ALTREP
object while `cow_b` still points at the original ALTREP object. That
automatic duplicate is handled by Rfmalloc’s ALTREP `Duplicate` method,
not by R’s ordinary `allocVector()` path.

`lobstr::obj_addr()` shows the R object header address, not necessarily
the data payload pointer. The payload pointer returned by `DATAPTR()` is
a real writable pointer into the fmalloc mapping for fixed-width atomic
types. Character vectors store string bytes in fmalloc and materialize
ordinary R `CHARSXP` values on `STRING_ELT()` access. List vectors use
ALTREP list element methods plus an R-visible sidecar so the GC can see
referenced R objects.

## Why ALTREP Is Required for Copy-on-Write Control

Plain vectors allocated through custom allocator hooks do not control
R’s ordinary duplication path. R saves enough allocator state to free
those objects, but ordinary vector duplication does not consult a
per-object allocator copy callback, and the `R_allocator_t::res` field
is reserved.

Rfmalloc therefore constructs ALTREP vectors from the beginning. The
ALTREP C header declares a `Duplicate` method slot, so copy-on-write can
create another fmalloc-backed vector instead of falling back to R’s
ordinary heap allocator. For all current vector constructors, this also
means Rfmalloc no longer needs the non-API `Rf_allocVector3()` path.

The public ALTREP header used by the installed R on this system exposes
`Get_region` hooks for atomic vector reads and `Set_elt` hooks for
strings and lists, but not an atomic `Set_region` hook. Atomic writes
therefore go through R’s normal writable `DATAPTR()` path; internal bulk
copies use the direct fmalloc payload pointer.

## References

- fmalloc: <https://github.com/yasukata/fmalloc>
- Simon Urbanek’s mmap allocator example:
  <https://gist.github.com/s-u/6712c97ca74181f5a1a5>
- UFOs: <https://github.com/PRL-PRG/UFOs>
- Travel: <https://github.com/Jiefei-Wang/Travel>

## License

GPL (\>= 2)
