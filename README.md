
# Rfmalloc

Rfmalloc is an experimental R package for allocating vector storage from
a memory-mapped backing file using a patched copy of
[fmalloc](https://github.com/yasukata/fmalloc). The goal is to explore
large, file-backed R vector storage without routing large allocations
back through the process heap.

The current package exposes an `Rf_allocVector3()`-based custom
allocator for integer, numeric, and logical vectors. This is useful for
experiments and stress testing, but `Rf_allocVector3()` is not part of
R’s public API, so this package should still be treated as research
software.

## Current Status

Recent allocator work fixed the stress-path that previously fell back
into ptmalloc/dlmalloc’s system `mmap()` path for large requests. Large
allocations are now carved from contiguous runs in the fmalloc backing
file instead.

Implemented now:

- file-backed allocation for integer, numeric, and logical vectors;
- large allocations spanning multiple fmalloc chunks;
- `malloc`, `free`, and `realloc` support through the patched fmalloc
  layer;
- reopening backing files created by the current allocator format.

Still experimental / future work:

- ALTREP wrappers so R-level duplication, subsetting, and copy-on-write
  can stay file-backed;
- richer persistence semantics and metadata;
- support for more vector types;
- production-grade lifecycle management for mapped files.

## Installation

``` r
# install.packages("devtools")
devtools::install_github("sounkou-bioinfo/Rfmalloc")
```

## Basic Usage

``` r
library(Rfmalloc)

alloc_file <- tempfile(fileext = ".bin")
init_fmalloc(alloc_file)
#> Creating file with size: 33562624 bytes (0.03 GB)
#> fmalloc initialized with file: /tmp/RtmpISOFfQ/file1e9f634ccc1d9.bin (init: true)
#> [1] TRUE

v_int <- create_fmalloc_vector("integer", 10)
v_num <- create_fmalloc_vector("numeric", 10)

v_int[1:3] <- c(1L, 2L, 3L)
v_num[1:3] <- c(1.1, 2.2, 3.3)

v_int[1:3]
#> [1] 1 2 3
v_num[1:3]
#> [1] 1.1 2.2 3.3

rm(v_int, v_num)
gc()
#>          used (Mb) gc trigger (Mb) max used (Mb)
#> Ncells 523591 28.0    1136837 60.8   718274 38.4
#> Vcells 984631  7.6    8388608 64.0  2007149 15.4
cleanup_fmalloc()
#> Cleaning up fmalloc...
#> fmalloc cleaned up
unlink(alloc_file)
```

## Larger Allocation Example

``` r
library(Rfmalloc)

large_file <- tempfile(fileext = ".bin")
init_fmalloc(large_file, size_gb = 0.1)
#> Requested file size: 0.10 GB (107374182 bytes)
#> Creating file with size: 107374182 bytes (0.10 GB)
#> fmalloc initialized with file: /tmp/RtmpISOFfQ/file1e9f626b54ca5.bin (init: true)
#> [1] TRUE

# About 20 MB of integer payload, larger than the historical 16 MB chunk limit.
big_int <- create_fmalloc_vector("integer", 5e6)
#> Creating fmalloc vector: type=13, length=5000000
#> Large allocation: 19.07 MB requested
#> SUCCESS: fmalloc allocated 20000080 bytes
#> Successfully created fmalloc vector
big_int[1:5] <- 1:5
big_int[1:5]
#> [1] 1 2 3 4 5

rm(big_int)
gc()
#>          used (Mb) gc trigger (Mb) max used (Mb)
#> Ncells 523849 28.0    1136837 60.8   718274 38.4
#> Vcells 985509  7.6    8388608 64.0  3734700 28.5
cleanup_fmalloc()
#> Cleaning up fmalloc...
#> fmalloc cleaned up
unlink(large_file)
```

## Reopening a Backing File

The allocator metadata lives in the mapped file, so a backing file
created by the current format can be closed and reopened. Today this
demonstrates allocator state reuse; recovering named R objects still
needs a higher-level root-object or ALTREP layer.

``` r
library(Rfmalloc)

reopen_file <- tempfile(fileext = ".bin")

first_init <- init_fmalloc(reopen_file)
#> Creating file with size: 33562624 bytes (0.03 GB)
#> fmalloc initialized with file: /tmp/RtmpISOFfQ/file1e9f6256d02ac.bin (init: true)
first_vec <- create_fmalloc_vector("integer", 100)
first_vec[1:3] <- 1:3
first_init
#> [1] TRUE

rm(first_vec)
gc()
#>          used (Mb) gc trigger (Mb) max used (Mb)
#> Ncells 523954 28.0    1136837 60.8   718274 38.4
#> Vcells 985713  7.6    8388608 64.0  3734700 28.5
cleanup_fmalloc()
#> Cleaning up fmalloc...
#> fmalloc cleaned up

second_init <- init_fmalloc(reopen_file)
#> Using existing file: /tmp/RtmpISOFfQ/file1e9f6256d02ac.bin (size: 33562624 bytes)
#> fmalloc initialized with file: /tmp/RtmpISOFfQ/file1e9f6256d02ac.bin (init: false)
second_vec <- create_fmalloc_vector("numeric", 100)
second_vec[1:3] <- c(10.5, 20.5, 30.5)
second_init
#> [1] FALSE
second_vec[1:3]
#> [1] 10.5 20.5 30.5

rm(second_vec)
gc()
#>          used (Mb) gc trigger (Mb) max used (Mb)
#> Ncells 524004 28.0    1136837 60.8   718274 38.4
#> Vcells 985800  7.6    8388608 64.0  3734700 28.5
cleanup_fmalloc()
#> Cleaning up fmalloc...
#> fmalloc cleaned up
unlink(reopen_file)
```

## Watching R’s Duplication with `lobstr`

`lobstr::obj_addr()` and R’s internal inspector are useful for seeing
why the current custom-allocator approach is not enough. The initial
vector is backed by fmalloc, but when R performs copy-on-write it
duplicates through R’s ordinary vector allocation path.

``` r
library(Rfmalloc)
library(lobstr)

addr_file <- tempfile(fileext = ".bin")
init_fmalloc(addr_file)
#> Creating file with size: 33562624 bytes (0.03 GB)
#> fmalloc initialized with file: /tmp/RtmpISOFfQ/file1e9f672de8bd4.bin (init: true)
#> [1] TRUE

cow_a <- create_fmalloc_vector("integer", 10)
cow_a[1:3] <- 1:3
cow_b <- cow_a

data.frame(
  object = c("cow_a", "cow_b"),
  address = c(obj_addr(cow_a), obj_addr(cow_b))
)
#>   object        address
#> 1  cow_a 0x62bcf4b56c28
#> 2  cow_b 0x62bcf4b56c28
capture.output(.Internal(inspect(cow_a)))[1]
#> [1] "@62bcf4b56c28 13 INTSXP g0c4 [REF(3)] (len=10, tl=0) 1,2,3,0,0,..."

tracemem(cow_a)
#> [1] "<0x62bcf4b56c28>"
cow_a[1] <- 99L
#> tracemem[0x62bcf4b56c28 -> 0x62bcf6e04428]: eval eval withVisible withCallingHandlers eval eval with_handlers doWithOneRestart withOneRestart withRestartList doWithOneRestart withOneRestart withRestartList withRestarts <Anonymous> evaluate in_dir in_input_dir eng_r block_exec call_block process_group withCallingHandlers with_options <Anonymous> process_file <Anonymous> <Anonymous>

data.frame(
  object = c("cow_a", "cow_b"),
  address = c(obj_addr(cow_a), obj_addr(cow_b))
)
#>   object        address
#> 1  cow_a 0x62bcf6e04428
#> 2  cow_b 0x62bcf4b56c28
capture.output(.Internal(inspect(cow_a)))[1]
#> [1] "@62bcf6e04428 13 INTSXP g0c4 [REF(1),TR] (len=10, tl=0) 99,2,3,0,0,..."
capture.output(.Internal(inspect(cow_b)))[1]
#> [1] "@62bcf4b56c28 13 INTSXP g0c4 [REF(3),TR] (len=10, tl=0) 1,2,3,0,0,..."

rm(cow_a, cow_b)
gc()
#>          used (Mb) gc trigger (Mb) max used (Mb)
#> Ncells 530655 28.4    1136837 60.8   734646 39.3
#> Vcells 999833  7.7    8388608 64.0  3734700 28.5
cleanup_fmalloc()
#> Cleaning up fmalloc...
#> fmalloc cleaned up
unlink(addr_file)
```

The two names start as references to the same `SEXP`. After the write to
`cow_a`, `tracemem()` reports a duplicate and `lobstr::obj_addr()` shows
that `cow_a` moved to a new object while `cow_b` still points at the
original object. That automatic duplicate is made by R’s normal
`allocVector()` path, not by the fmalloc allocator used for the original
`Rf_allocVector3()` call.

`lobstr::obj_addr()` shows the R object header address, not necessarily
the data payload pointer. For allocator debugging we may add a small
`.Call()` helper that prints both the `SEXP` address and `DATAPTR()` and
whether the payload lies inside the current fmalloc mapping.

## Why ALTREP Is Still the Likely Next Step

Allocating the initial vector storage in the backing file is not enough
to make all R operations file-backed. R’s normal duplication paths often
call `allocVector()`, not `Rf_allocVector3()`, so copy-on-write or
subsetting may produce ordinary heap-backed vectors.

We cannot already pass an automatic duplication method through the
current standard-vector custom allocator. R saves the custom allocator
so the original object can be freed correctly, but ordinary vector
duplication does not consult a per-object allocator hook. The
`R_allocator_t::res` field is reserved and is not a supported copy
callback.

A UFO/Travel-like direction is still interesting, but for this package
the next practical layer is likely ALTREP: implement `Dataptr`, `Elt`,
`Get_region`, and especially `Duplicate` so R can preserve file-backed
storage during ordinary vector operations. The ALTREP `Duplicate` method
is the supported place where we can allocate the copied vector through
fmalloc as well. Pointer-access instrumentation is only needed if we
want more transparent interception than ALTREP provides.

## References

- fmalloc: <https://github.com/yasukata/fmalloc>
- Simon Urbanek’s mmap allocator example:
  <https://gist.github.com/s-u/6712c97ca74181f5a1a5>
- UFOs: <https://github.com/PRL-PRG/UFOs>
- Travel: <https://github.com/Jiefei-Wang/Travel>

## License

GPL (\>= 2)
