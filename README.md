
# Rfmalloc

Rfmalloc is an experimental R package for allocating vector storage from
a memory-mapped backing file using a patched copy of
[fmalloc](https://github.com/yasukata/fmalloc). The goal is to explore
large, file-backed R vector storage without routing large allocations
back through the process heap.

The current package exposes ALTREP vectors whose fixed-width atomic
payloads are allocated directly from fmalloc. This uses the ALTREP C
entry points available in R’s installed headers instead of relying on
the non-API `Rf_allocVector3()` vector allocator hook.

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
- native lifetime tracking from ALTREP vector handles to runtime
  mappings, so a runtime mapping is not destroyed while vectors
  allocated from it are still reachable.

Still experimental / future work:

- ALTREP `Extract_subset` methods for operations such as `x[i]` when
  their results should remain fmalloc-backed; today those operations may
  return ordinary R heap vectors;
- a real allocation catalog for listing and validating persistent
  allocations;
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

The shortest form uses `init_fmalloc()`, which opens a backing file and
stores it as the package default runtime. Calls to
`create_fmalloc_vector()` can then omit the `runtime` argument:

``` r
library(Rfmalloc)

alloc_file <- tempfile(fileext = ".bin")
init_fmalloc(alloc_file)
#> Creating file with size: 33562624 bytes (0.03 GB)
#> fmalloc initialized with file: /tmp/RtmpO7TKZ7/file61ddb4a833eab.bin (init: true, mode: persistent)
#> [1] TRUE

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

v_int[1:3]
#> [1] 1 2 3
v_num[1:3]
#> [1] 1.1 2.2 3.3
v_raw
#> [1] 01 02 03 04
v_cplx
#> [1] 1+1i 2+2i 3+3i 4+4i
v_chr
#> [1] "alpha" "beta"  NA
v_lst
#> [[1]]
#>   x
#> 1 1
#> 2 2
#> 
#> [[2]]
#> NULL

rm(v_int, v_num, v_raw, v_cplx, v_chr, v_lst)
gc()
#>          used (Mb) gc trigger (Mb) max used (Mb)
#> Ncells 529124 28.3    1152645 61.6   718274 38.4
#> Vcells 998856  7.7    8388608 64.0  2007149 15.4
cleanup_fmalloc()
#> Cleaning up fmalloc...
#> fmalloc cleaned up
unlink(alloc_file)
```

For multiple backing files or explicit lifetime management, prefer
runtime handles returned by `open_fmalloc()`:

``` r
library(Rfmalloc)

handle_file <- tempfile(fileext = ".bin")
rt <- open_fmalloc(handle_file)
#> Creating file with size: 33562624 bytes (0.03 GB)
#> fmalloc initialized with file: /tmp/RtmpO7TKZ7/file61ddb38d2fdf2.bin (init: true, mode: persistent)

v <- create_fmalloc_vector("integer", 10, runtime = rt)
v[1:3] <- 10:12
v[1:3]
#> [1] 10 11 12

rm(v)
gc()
#>          used (Mb) gc trigger (Mb) max used (Mb)
#> Ncells 529198 28.3    1152645 61.6   718274 38.4
#> Vcells 999574  7.7    8388608 64.0  2007149 15.4
cleanup_fmalloc(rt)
#> Cleaning up fmalloc...
#> fmalloc cleaned up
unlink(handle_file)
```

## Larger Allocation Example

This is the kind of stress case the package is meant for: it creates a 5
GB backing file and allocates about 4 GB of integer payload.

``` r
library(Rfmalloc)

large_file <- tempfile(fileext = ".bin")
init_fmalloc(large_file, size_gb = 5)
#> Requested file size: 5.00 GB (5368709120 bytes)
#> Creating file with size: 5368709120 bytes (5.00 GB)
#> fmalloc initialized with file: /tmp/RtmpO7TKZ7/file61ddb46359e66.bin (init: true, mode: persistent)
#> [1] TRUE

# 1 billion integers = about 4 GB of payload, backed by fmalloc.
# Keep creation and initialization in one local expression so README rendering
# does not keep an extra transient reference that would force a second 4 GB COW
# duplicate during `[<-`.
big_int <- local({
  x <- create_fmalloc_vector("integer", 1e9)
  x[1:5] <- 1:5
  x
})
#> Creating fmalloc ALTREP vector: type=integer, length=1000000000
#> Large allocation: 3814.70 MB requested
#> SUCCESS: fmalloc allocated 4000000000 bytes
#> Successfully created fmalloc ALTREP vector
big_int[1:5]
#> [1] 1 2 3 4 5

rm(big_int)
gc()
#>          used (Mb) gc trigger (Mb) max used (Mb)
#> Ncells 529247 28.3    1152645 61.6   718274 38.4
#> Vcells 999781  7.7    8388608 64.0  2007149 15.4
cleanup_fmalloc()
#> Cleaning up fmalloc...
#> fmalloc cleaned up
unlink(large_file)
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

persist_file <- tempfile(fileext = ".bin")
rt <- open_fmalloc(persist_file, mode = "persistent")
#> Creating file with size: 33562624 bytes (0.03 GB)
#> fmalloc initialized with file: /tmp/RtmpO7TKZ7/file61ddb3ca1e24d.bin (init: true, mode: persistent)

x <- create_fmalloc_vector("integer", 5, runtime = rt)
x[] <- 1:5
roundtrip <- unserialize(serialize(x, NULL))
#> Using existing file: /tmp/RtmpO7TKZ7/file61ddb3ca1e24d.bin (size: 33562624 bytes)
#> fmalloc initialized with file: /tmp/RtmpO7TKZ7/file61ddb3ca1e24d.bin (init: false, mode: persistent)
roundtrip[]
#> [1] 1 2 3 4 5

rm(x, roundtrip)
gc()
#>           used (Mb) gc trigger (Mb) max used (Mb)
#> Ncells  529309 28.3    1152645 61.6   718274 38.4
#> Vcells 1000017  7.7    8388608 64.0  2007149 15.4
cleanup_fmalloc(rt)
#> Cleaning up fmalloc...
#> fmalloc cleaned up
unlink(persist_file)
```

## Inspecting fmalloc ALTREP Metadata

R’s internal inspector dispatches to the ALTREP `Inspect` hook.
Rfmalloc’s inspector reports the vector type, length, payload byte
count, runtime mode, runtime state, payload offset, file UUID, and
backing-file path.

``` r
library(Rfmalloc)

inspect_file <- tempfile(fileext = ".bin")
rt <- open_fmalloc(inspect_file, mode = "persistent")
#> Creating file with size: 33562624 bytes (0.03 GB)
#> fmalloc initialized with file: /tmp/RtmpO7TKZ7/file61ddb2676c8da.bin (init: true, mode: persistent)

inspect_vec <- create_fmalloc_vector("integer", 4, runtime = rt)
inspect_vec[] <- 1:4
.Internal(inspect(inspect_vec))
#> @596f08fcdfa0 13 INTSXP g0c0 [REF(1)] fmalloc_altrep type=integer length=4 bytes=16 data=0x735592e02408 mode=persistent runtime=open offset=9224 uuid=b71908cb79fb031ff1bd5fe2a537df6a file=/tmp/RtmpO7TKZ7/file61ddb2676c8da.bin

rm(inspect_vec)
gc()
#>           used (Mb) gc trigger (Mb) max used (Mb)
#> Ncells  529285 28.3    1152645 61.6   718274 38.4
#> Vcells 1000122  7.7    8388608 64.0  2007149 15.4
cleanup_fmalloc(rt)
#> Cleaning up fmalloc...
#> fmalloc cleaned up
unlink(inspect_file)
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

char_file <- tempfile(fileext = ".bin")
rt <- open_fmalloc(char_file, mode = "persistent")
#> Creating file with size: 33562624 bytes (0.03 GB)
#> fmalloc initialized with file: /tmp/RtmpO7TKZ7/file61ddb72e65f94.bin (init: true, mode: persistent)

chars <- create_fmalloc_vector("character", 3, runtime = rt)
chars[] <- c("one", NA_character_, "three")
chars[]
#> [1] "one"   NA      "three"

from_int <- create_fmalloc_vector("integer", 3, runtime = rt)
from_int[] <- 1:3
as.character(from_int)[]
#> [1] "1" "2" "3"

rm(chars, from_int)
gc()
#>           used (Mb) gc trigger (Mb) max used (Mb)
#> Ncells  529386 28.3    1152645 61.6   718274 38.4
#> Vcells 1000303  7.7    8388608 64.0  2007149 15.4
cleanup_fmalloc(rt)
#> Cleaning up fmalloc...
#> fmalloc cleaned up
unlink(char_file)
```

## Multiple Runtimes and Lifetime

Runtime handles make it possible to use more than one backing file in
the same R process. Allocator entry is serialized with native mutexes
because the current fmalloc/ptmalloc layer still has process-global
target state internally.

``` r
library(Rfmalloc)

file_a <- tempfile(fileext = ".bin")
file_b <- tempfile(fileext = ".bin")

rt_a <- open_fmalloc(file_a)
#> Creating file with size: 33562624 bytes (0.03 GB)
#> fmalloc initialized with file: /tmp/RtmpO7TKZ7/file61ddb598eae91.bin (init: true, mode: persistent)
rt_b <- open_fmalloc(file_b, size_gb = 0.1)
#> Requested file size: 0.10 GB (107374182 bytes)
#> Creating file with size: 107374182 bytes (0.10 GB)
#> fmalloc initialized with file: /tmp/RtmpO7TKZ7/file61ddb4a83fbe3.bin (init: true, mode: persistent)

vec_a <- create_fmalloc_vector("integer", 10, runtime = rt_a)
vec_b <- create_fmalloc_vector("numeric", 10, runtime = rt_b)
vec_a[1] <- 101L
vec_b[1] <- 202

data.frame(
  vector = c("vec_a", "vec_b"),
  value = c(vec_a[1], vec_b[1])
)
#>   vector value
#> 1  vec_a   101
#> 2  vec_b   202

cleanup_fmalloc(rt_a)
#> Cleaning up fmalloc...
#> fmalloc cleaned up
vec_a[1]
#> [1] 101

rm(vec_a)
gc()
#>           used (Mb) gc trigger (Mb) max used (Mb)
#> Ncells  530500 28.4    1152645 61.6   718274 38.4
#> Vcells 1002917  7.7    8388608 64.0  2007149 15.4
cleanup_fmalloc(rt_b)
#> Cleaning up fmalloc...
#> fmalloc cleaned up
rm(vec_b)
gc()
#>           used (Mb) gc trigger (Mb) max used (Mb)
#> Ncells  530501 28.4    1152645 61.6   718274 38.4
#> Vcells 1002930  7.7    8388608 64.0  2007149 15.4
unlink(c(file_a, file_b))
```

Calling `cleanup_fmalloc(rt_a)` marks `rt_a` closed. If vectors from
`rt_a` are still reachable, the native mapping is kept alive until those
vectors are garbage-collected. This is implemented with a native
live-vector count and an ALTREP-held external pointer for each
fmalloc-backed vector; no user-visible attribute is added to the vector.

## Reopening and Persistent References

A backing file can be reopened. Persistent-mode fixed-width atomic and
character ALTREP vectors can also be serialized by reference: the
serialized state stores the backing path, file UUID, type, length,
payload offset, and byte size. During unserialization, Rfmalloc reopens
the file, verifies the UUID, and reconstructs an ALTREP around the same
allocation.

``` r
library(Rfmalloc)

reopen_file <- tempfile(fileext = ".bin")

rt <- open_fmalloc(reopen_file, mode = "persistent")
#> Creating file with size: 33562624 bytes (0.03 GB)
#> fmalloc initialized with file: /tmp/RtmpO7TKZ7/file61ddb71584b7d.bin (init: true, mode: persistent)
first_vec <- create_fmalloc_vector("integer", 100, runtime = rt)
first_vec[1:3] <- 1:3
blob <- serialize(first_vec, NULL)

rm(first_vec)
gc()
#>           used (Mb) gc trigger (Mb) max used (Mb)
#> Ncells  530440 28.4    1152645 61.6   718274 38.4
#> Vcells 1003054  7.7    8388608 64.0  2007149 15.4
cleanup_fmalloc(rt)
#> Cleaning up fmalloc...
#> fmalloc cleaned up
rm(rt)
gc()
#>           used (Mb) gc trigger (Mb) max used (Mb)
#> Ncells  530435 28.4    1152645 61.6   718274 38.4
#> Vcells 1003059  7.7    8388608 64.0  2007149 15.4

recovered <- unserialize(blob)
#> Using existing file: /tmp/RtmpO7TKZ7/file61ddb71584b7d.bin (size: 33562624 bytes)
#> fmalloc initialized with file: /tmp/RtmpO7TKZ7/file61ddb71584b7d.bin (init: false, mode: persistent)
recovered[1:3]
#> [1] 1 2 3
typeof(recovered)
#> [1] "integer"

rm(recovered)
gc()
#>           used (Mb) gc trigger (Mb) max used (Mb)
#> Ncells  530478 28.4    1152645 61.6   718274 38.4
#> Vcells 1003157  7.7    8388608 64.0  2007149 15.4
unlink(reopen_file)
```

This is reference-based recovery, not name-based object discovery.
Rfmalloc does not yet keep a complete in-file object catalog that can
list all allocations or recover dropped vectors by user names. The
serialized reference itself is the locator for now.

## Watching ALTREP-Controlled Duplication with `lobstr`

`lobstr::obj_addr()` is useful for seeing why the vectors are ALTREP
from the beginning. R’s copy-on-write path dispatches to the ALTREP
`Duplicate` method, and the duplicate allocates its payload from
fmalloc.

``` r
library(Rfmalloc)
library(lobstr)

addr_file <- tempfile(fileext = ".bin")
init_fmalloc(addr_file)
#> Creating file with size: 33562624 bytes (0.03 GB)
#> fmalloc initialized with file: /tmp/RtmpO7TKZ7/file61ddb4fee1ad4.bin (init: true, mode: persistent)
#> [1] TRUE

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

before
#>   object        address
#> 1  cow_a 0x596f08ae6840
#> 2  cow_b 0x596f08ae6840
after
#>   object        address
#> 1  cow_a 0x596f0899f130
#> 2  cow_b 0x596f08ae6840

rm(cow_a, cow_b)
gc()
#>           used (Mb) gc trigger (Mb) max used (Mb)
#> Ncells  533062 28.5    1152645 61.6   718274 38.4
#> Vcells 1007196  7.7    8388608 64.0  2007149 15.4
cleanup_fmalloc()
#> Cleaning up fmalloc...
#> fmalloc cleaned up
unlink(addr_file)
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
