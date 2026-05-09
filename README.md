
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
- reopening backing files created by the current allocator format;
- explicit rejection of older incompatible fmalloc backing files.

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
#> fmalloc initialized with file: /tmp/RtmpyiFs61/file148762fbedc99.bin (init: true)
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
#> Ncells 523482 28.0    1136525 60.7   718274 38.4
#> Vcells 983636  7.6    8388608 64.0  2007149 15.4
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
#> fmalloc initialized with file: /tmp/RtmpyiFs61/file148763caeb430.bin (init: true)
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
#> Ncells 523740 28.0    1136525 60.7   718274 38.4
#> Vcells 984514  7.6    8388608 64.0  3733703 28.5
cleanup_fmalloc()
#> Cleaning up fmalloc...
#> fmalloc cleaned up
unlink(large_file)
```

## Why ALTREP Is Still the Likely Next Step

Allocating the initial vector storage in the backing file is not enough
to make all R operations file-backed. R’s normal duplication paths often
call `allocVector()`, not `Rf_allocVector3()`, so copy-on-write or
subsetting may produce ordinary heap-backed vectors.

A UFO/Travel-like direction is still interesting, but for this package
the next practical layer is likely ALTREP: implement `Dataptr`, `Elt`,
`Get_region`, and especially `Duplicate` so R can preserve file-backed
storage during ordinary vector operations. Pointer-access
instrumentation is only needed if we want more transparent interception
than ALTREP provides.

## References

- fmalloc: <https://github.com/yasukata/fmalloc>
- Simon Urbanek’s mmap allocator example:
  <https://gist.github.com/s-u/6712c97ca74181f5a1a5>
- UFOs: <https://github.com/PRL-PRG/UFOs>
- Travel: <https://github.com/Jiefei-Wang/Travel>

## License

GPL (\>= 2)
