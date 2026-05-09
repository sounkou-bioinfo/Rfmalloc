
# Rfmalloc

Rfmalloc is an experimental R package for allocating vector storage from
a memory-mapped backing file using a patched copy of
[fmalloc](https://github.com/yasukata/fmalloc). The goal is to explore
large, file-backed R vector storage without routing large allocations
back through the process heap.

The current package exposes ALTREP vectors whose fixed-width atomic
payloads are allocated directly from fmalloc. This keeps the
implementation on R’s documented C/ALTREP surface instead of relying on
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
- native lifetime tracking from ALTREP vector handles to runtime
  mappings, so a runtime mapping is not destroyed while vectors
  allocated from it are still reachable.

Still experimental / future work:

- ALTREP `Extract_subset` and `Coerce` methods for operations such as
  `x[i]` and `as.numeric(x)` when their results should remain
  fmalloc-backed; today those operations may return ordinary R heap
  vectors;
- richer persistence semantics and metadata;
- richer persistence semantics for pointer-containing R types; character
  and list vectors are session-local because their elements are R object
  pointers;
- production-grade lifecycle management for mapped files.

## Installation

``` r
# install.packages("devtools")
devtools::install_github("sounkou-bioinfo/Rfmalloc")
```

## Basic Usage

The compatibility API keeps a package-default runtime:

``` r
library(Rfmalloc)

alloc_file <- tempfile(fileext = ".bin")
init_fmalloc(alloc_file)
#> Creating file with size: 33562624 bytes (0.03 GB)
#> fmalloc initialized with file: /tmp/RtmpKfatap/file388be485d7c48.bin (init: true)
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
v_chr[] <- c("a", "b", "c")
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
#> [1] "a" "b" "c"
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
#> Ncells 528940 28.3    1152120 61.6   718274 38.4
#> Vcells 997270  7.7    8388608 64.0  2007149 15.4
cleanup_fmalloc()
#> Cleaning up fmalloc...
#> fmalloc cleaned up
unlink(alloc_file)
```

For new code, prefer explicit runtime handles:

``` r
library(Rfmalloc)

handle_file <- tempfile(fileext = ".bin")
rt <- open_fmalloc(handle_file)
#> Creating file with size: 33562624 bytes (0.03 GB)
#> fmalloc initialized with file: /tmp/RtmpKfatap/file388be2fa9b782.bin (init: true)

v <- create_fmalloc_vector("integer", 10, runtime = rt)
v[1:3] <- 10:12
v[1:3]
#> [1] 10 11 12

rm(v)
gc()
#>          used (Mb) gc trigger (Mb) max used (Mb)
#> Ncells 529008 28.3    1152120 61.6   718274 38.4
#> Vcells 997970  7.7    8388608 64.0  2007149 15.4
cleanup_fmalloc(rt)
#> Cleaning up fmalloc...
#> fmalloc cleaned up
unlink(handle_file)
```

## Larger Allocation Example

``` r
library(Rfmalloc)

large_file <- tempfile(fileext = ".bin")
init_fmalloc(large_file, size_gb = 0.1)
#> Requested file size: 0.10 GB (107374182 bytes)
#> Creating file with size: 107374182 bytes (0.10 GB)
#> fmalloc initialized with file: /tmp/RtmpKfatap/file388be24c613be.bin (init: true)
#> [1] TRUE

# About 20 MB of integer payload, larger than the historical 16 MB chunk limit.
big_int <- create_fmalloc_vector("integer", 5e6)
#> Creating fmalloc ALTREP vector: type=integer, length=5000000
#> Large allocation: 19.07 MB requested
#> SUCCESS: fmalloc allocated 20000000 bytes
#> Successfully created fmalloc ALTREP vector
big_int[1:5] <- 1:5
#> Large allocation: 19.07 MB requested
#> SUCCESS: fmalloc allocated 20000000 bytes
big_int[1:5]
#> [1] 1 2 3 4 5

rm(big_int)
gc()
#>          used (Mb) gc trigger (Mb) max used (Mb)
#> Ncells 529033 28.3    1152120 61.6   718274 38.4
#> Vcells 998137  7.7    8388608 64.0  2007149 15.4
cleanup_fmalloc()
#> Cleaning up fmalloc...
#> fmalloc cleaned up
unlink(large_file)
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
#> fmalloc initialized with file: /tmp/RtmpKfatap/file388be24ea45b5.bin (init: true)
rt_b <- open_fmalloc(file_b, size_gb = 0.1)
#> Requested file size: 0.10 GB (107374182 bytes)
#> Creating file with size: 107374182 bytes (0.10 GB)
#> fmalloc initialized with file: /tmp/RtmpKfatap/file388be4588b1b3.bin (init: true)

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
#> Ncells  530213 28.4    1152120 61.6   718274 38.4
#> Vcells 1000791  7.7    8388608 64.0  2007149 15.4
cleanup_fmalloc(rt_b)
#> Cleaning up fmalloc...
#> fmalloc cleaned up
rm(vec_b)
gc()
#>           used (Mb) gc trigger (Mb) max used (Mb)
#> Ncells  530208 28.4    1152120 61.6   718274 38.4
#> Vcells 1000798  7.7    8388608 64.0  2007149 15.4
unlink(c(file_a, file_b))
```

Calling `cleanup_fmalloc(rt_a)` marks `rt_a` closed. If vectors from
`rt_a` are still reachable, the native mapping is kept alive until those
vectors are garbage-collected. This is implemented with a native
live-vector count and an ALTREP-held external pointer for each
fmalloc-backed vector; no user-visible attribute is added to the vector.

## Reopening Is Not Vector Recovery Yet

A backing file can be reopened, but this currently reopens only the
allocator state. We are **not** getting the R vector object back yet.
The package does not currently store a root table such as
`(name, type, length, offset)`, so an ALTREP vector cannot yet be safely
reconstructed after it has been dropped by R’s garbage collector.

``` r
library(Rfmalloc)

reopen_file <- tempfile(fileext = ".bin")

first_init <- init_fmalloc(reopen_file)
#> Creating file with size: 33562624 bytes (0.03 GB)
#> fmalloc initialized with file: /tmp/RtmpKfatap/file388be722b6386.bin (init: true)
first_vec <- create_fmalloc_vector("integer", 100)
first_vec[1:3] <- 1:3
first_init
#> [1] TRUE
first_vec[1:3]
#> [1] 1 2 3

rm(first_vec)
gc()
#>           used (Mb) gc trigger (Mb) max used (Mb)
#> Ncells  530123 28.4    1152120 61.6   718274 38.4
#> Vcells 1000823  7.7    8388608 64.0  2007149 15.4
cleanup_fmalloc()
#> Cleaning up fmalloc...
#> fmalloc cleaned up

second_init <- init_fmalloc(reopen_file)
#> Using existing file: /tmp/RtmpKfatap/file388be722b6386.bin (size: 33562624 bytes)
#> fmalloc initialized with file: /tmp/RtmpKfatap/file388be722b6386.bin (init: false)
second_init
#> [1] FALSE
exists("first_vec")
#> [1] FALSE

cleanup_fmalloc()
#> Cleaning up fmalloc...
#> fmalloc cleaned up
unlink(reopen_file)
```

The `FALSE` from `second_init` tells us the file already contained
current fmalloc allocator metadata. It does **not** mean `first_vec` was
recovered. Real object recovery needs a higher-level object catalog and
likely an ALTREP vector constructor that can recreate an R vector from
`(file, offset, type, length)`.

## Watching ALTREP-Controlled Duplication with `lobstr`

`lobstr::obj_addr()` and R’s internal inspector are useful for seeing
why the vectors are ALTREP from the beginning. R’s copy-on-write path
dispatches to the ALTREP `Duplicate` method, and the duplicate allocates
its payload from fmalloc.

``` r
library(Rfmalloc)
library(lobstr)

addr_file <- tempfile(fileext = ".bin")
init_fmalloc(addr_file)
#> Creating file with size: 33562624 bytes (0.03 GB)
#> fmalloc initialized with file: /tmp/RtmpKfatap/file388be71652fcc.bin (init: true)
#> [1] TRUE

cow_a <- create_fmalloc_vector("integer", 10)
cow_a[1:3] <- 1:3
cow_b <- cow_a

data.frame(
  object = c("cow_a", "cow_b"),
  address = c(obj_addr(cow_a), obj_addr(cow_b))
)
#>   object        address
#> 1  cow_a 0x5e4eea76d8b8
#> 2  cow_b 0x5e4eea76d8b8
capture.output(.Internal(inspect(cow_a)))[1]
#> [1] "@5e4eea76d8b8 13 INTSXP g0c0 [REF(3)] fmalloc_altrep integer length=10 data=0x7c1c43802418 bytes=40"

tracemem(cow_a)
#> [1] "<0x5e4eea76d8b8>"
cow_a[1] <- 99L
#> tracemem[0x5e4eea76d8b8 -> 0x5e4ee960ffb0]: eval eval withVisible withCallingHandlers eval eval with_handlers doWithOneRestart withOneRestart withRestartList doWithOneRestart withOneRestart withRestartList withRestarts <Anonymous> evaluate in_dir in_input_dir eng_r block_exec call_block process_group withCallingHandlers with_options <Anonymous> process_file <Anonymous> <Anonymous>

data.frame(
  object = c("cow_a", "cow_b"),
  address = c(obj_addr(cow_a), obj_addr(cow_b))
)
#>   object        address
#> 1  cow_a 0x5e4ee960ffb0
#> 2  cow_b 0x5e4eea76d8b8
capture.output(.Internal(inspect(cow_a)))[1]
#> [1] "@5e4ee960ffb0 13 INTSXP g0c0 [REF(1),TR] fmalloc_altrep integer length=10 data=0x7c1c43802448 bytes=40"
capture.output(.Internal(inspect(cow_b)))[1]
#> [1] "@5e4eea76d8b8 13 INTSXP g0c0 [REF(3),TR] fmalloc_altrep integer length=10 data=0x7c1c43802418 bytes=40"

rm(cow_a, cow_b)
gc()
#>           used (Mb) gc trigger (Mb) max used (Mb)
#> Ncells  533204 28.5    1152120 61.6   751781 40.2
#> Vcells 1006090  7.7    8388608 64.0  2007149 15.4
cleanup_fmalloc()
#> Cleaning up fmalloc...
#> fmalloc cleaned up
unlink(addr_file)
```

The two names start as references to the same `SEXP`. After the write to
`cow_a`, `tracemem()` reports a duplicate and `lobstr::obj_addr()` shows
that `cow_a` moved to a new ALTREP object while `cow_b` still points at
the original ALTREP object. That automatic duplicate is handled by
Rfmalloc’s ALTREP `Duplicate` method, not by R’s ordinary
`allocVector()` path.

`lobstr::obj_addr()` shows the R object header address, not necessarily
the data payload pointer. The payload pointer returned by `DATAPTR()` is
a real writable pointer into the fmalloc mapping for fixed-width atomic
types. Character and list vectors use ALTREP string/list element methods
plus an R-visible sidecar so the GC can see referenced R objects.

## Why ALTREP Is Required for Copy-on-Write Control

Plain vectors allocated through custom allocator hooks do not control
R’s ordinary duplication path. R saves enough allocator state to free
those objects, but ordinary vector duplication does not consult a
per-object allocator copy callback, and the `R_allocator_t::res` field
is reserved.

Rfmalloc therefore constructs ALTREP vectors from the beginning. ALTREP
gives the package a supported `Duplicate` method, so copy-on-write can
create another fmalloc-backed vector instead of falling back to R’s
ordinary heap allocator. For all current vector constructors, this also
means Rfmalloc no longer needs the non-API `Rf_allocVector3()` path.

## References

- fmalloc: <https://github.com/yasukata/fmalloc>
- Simon Urbanek’s mmap allocator example:
  <https://gist.github.com/s-u/6712c97ca74181f5a1a5>
- UFOs: <https://github.com/PRL-PRG/UFOs>
- Travel: <https://github.com/Jiefei-Wang/Travel>

## License

GPL (\>= 2)
