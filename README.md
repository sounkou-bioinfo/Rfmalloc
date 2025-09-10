
# Rfmalloc

<!-- badges: start -->

[![R-CMD-check](https://github.com/sounkou-bioinfo/Rfmalloc/workflows/R-CMD-check/badge.svg)](https://github.com/sounkou-bioinfo/Rfmalloc/actions)
<!-- badges: end -->

Rfmalloc provides memory-mapped file allocation capabilities for R using
the fmalloc library. It offers two main approaches for efficient
persistent storage and memory usage:

1.  **ALTREP-based memory-mapped vectors** - Custom vector types that
    directly map file contents to memory
2.  **Custom allocators using fmalloc** - Alternative memory allocation
    backed by memory-mapped files

## Installation

You can install the development version of Rfmalloc from
[GitHub](https://github.com/) with:

``` r
# install.packages("devtools")
devtools::install_github("sounkou-bioinfo/Rfmalloc")
```

## System Requirements

- C++11 compiler
- POSIX-compliant system (Linux, macOS)
- pthreads library

## Basic Usage

### Memory-Mapped Vectors (ALTREP)

Create persistent integer vectors backed by files:

``` r
library(Rfmalloc)

# Create a memory-mapped vector
vec_file <- tempfile(fileext = ".bin")
v <- create_mmap_vector(vec_file, length = 1000)

# Use like a regular vector
v[1:10] <- 1:10
print(v[1:10])
#>  [1]  1  2  3  4  5  6  7  8  9 10

# Data persists across sessions
v2 <- create_mmap_vector(vec_file, length = 1000)
print(v2[1:10])  # Same values as before
#>  [1]  1  2  3  4  5  6  7  8  9 10
```

### Custom Allocators with fmalloc

Use fmalloc for alternative memory allocation:

``` r

# Initialize fmalloc with a backing file
alloc_file <- tempfile(fileext = ".bin")
init_fmalloc(alloc_file)
#> fmalloc initialized with file: /tmp/RtmpWVGrDo/file83a026ca5c179.bin (init: true)
#> [1] TRUE

# Create vectors using fmalloc
v_int <- create_fmalloc_vector("integer", 100)
#> fmalloc allocated 480 bytes at 0x7479b04023e0
v_num <- create_fmalloc_vector("numeric", 100)
#> fmalloc allocated 880 bytes at 0x7479b04025c8
# Clean up
cleanup_fmalloc()
#> fmalloc cleaned up
```

## Technical Details

### ALTREP Implementation

The ALTREP (ALTernative REPresentations) implementation provides:

- Direct memory mapping of file contents
- Efficient access to large datasets
- Persistent storage across R sessions
- Memory-efficient operations on large vectors

### fmalloc Integration

The fmalloc allocator offers:

- Memory allocation backed by memory-mapped files
- Support for multiple data types (integer, numeric, logical)
- Custom memory management strategies
- Integration with R’s allocator framework

## Error Handling

The package includes comprehensive error handling for:

- Invalid file paths and permissions
- Memory allocation failures
- Type validation
- Resource cleanup

## Contributing

Please note that this project uses the fmalloc library from
<https://github.com/yasukata/fmalloc> and custom allocator concepts from
<https://gist.github.com/s-u/6712c97ca74181f5a1a5>.

## License

This project is licensed under GPL (\>= 2).
