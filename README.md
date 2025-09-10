
# Rfmalloc

Rfmalloc provides persistent memory allocation capabilities for R using
the fmalloc library. It offers file-backed memory allocation with full
malloc, free, and realloc support for efficient persistent storage.

## Key Features

- **File-backed allocation** - Memory allocated from memory-mapped files
- **Full realloc support** - Supports malloc, free, and realloc
  patterns  
- **Persistent storage** - Data persists across R sessions
- **Efficient memory management** - Based on ptmalloc3 algorithm

## Installation

``` r
# install.packages("devtools")
devtools::install_github("sounkou-bioinfo/Rfmalloc")
```

## Basic Usage

``` r
library(Rfmalloc)

# Initialize fmalloc with a backing file
alloc_file <- tempfile(fileext = ".bin")
init_fmalloc(alloc_file)
#> Creating file with size: 33554432 bytes (0.03 GB)
#> fmalloc initialized with file: /tmp/Rtmpvb2HXq/file30a564c404dd.bin (init: true)
#> [1] TRUE

# Create vectors using file-backed allocation
v_int <- create_fmalloc_vector("integer", 10)
#> Creating fmalloc vector: type=13, length=10
#> fmalloc allocated 120 bytes at 0x78cf654023e0
#> Successfully created fmalloc vector
v_num <- create_fmalloc_vector("numeric", 10)
#> Creating fmalloc vector: type=14, length=10
#> fmalloc allocated 160 bytes at 0x78cf65402460
#> Successfully created fmalloc vector

# Use the vectors normally
v_int[1:3] <- c(1L, 2L, 3L)
v_num[1:3] <- c(1.1, 2.2, 3.3)

print(v_int[1:3])
#> [1] 1 2 3
print(v_num[1:3])
#> [1] 1.1 2.2 3.3

# Clean up - force garbage collection before cleanup to avoid warnings  
rm(v_int, v_num)
gc()
#> fmalloc freed memory at 0x78cf654023e0
#> fmalloc freed memory at 0x78cf65402460
#>          used (Mb) gc trigger (Mb) max used (Mb)
#> Ncells 522114 27.9    1133045 60.6   717417 38.4
#> Vcells 986168  7.6    8388608 64.0  2021519 15.5
cleanup_fmalloc()
#> Cleaning up fmalloc...
#> fmalloc cleaned up
file.remove(alloc_file)
#> [1] TRUE
```

## Larger Examples

``` r
library(Rfmalloc)

# Create a 100MB backing file
large_file <- tempfile(fileext = ".bin")
init_fmalloc(large_file, size_gb = 0.1)
#> Requested file size: 0.10 GB (107374182 bytes)
#> Creating file with size: 107374182 bytes (0.10 GB)
#> fmalloc initialized with file: /tmp/Rtmpvb2HXq/file30a5101e2c8c.bin (init: true)
#> [1] TRUE

# Create larger vectors
vec1 <- create_fmalloc_vector("integer", 1000)
#> Creating fmalloc vector: type=13, length=1000
#> fmalloc allocated 4080 bytes at 0x78cf5ec023e0
#> Successfully created fmalloc vector
vec2 <- create_fmalloc_vector("numeric", 500)
#> Creating fmalloc vector: type=14, length=500
#> fmalloc allocated 4080 bytes at 0x78cf5ec033e0
#> Successfully created fmalloc vector

# Fill with data
vec1[1:10] <- 1:10
vec2[1:10] <- (1:10) * 1.5

cat("Sample data:", vec1[1:5], "\n")
#> Sample data: 1 2 3 4 5
cat("Random access:", vec1[sample(10, 3)], "\n")
#> Random access: 7 2 10

# Clean up - force garbage collection before cleanup to avoid warnings
rm(vec1, vec2)
gc()
#> fmalloc freed memory at 0x78cf5ec023e0
#> fmalloc freed memory at 0x78cf5ec033e0
#>          used (Mb) gc trigger (Mb) max used (Mb)
#> Ncells 522717 28.0    1133045 60.6   717417 38.4
#> Vcells 988002  7.6    8388608 64.0  2021519 15.5
cleanup_fmalloc()
#> Cleaning up fmalloc...
#> fmalloc cleaned up
file.remove(large_file)
#> [1] TRUE
```

## References

Please note that this project uses the fmalloc library from
<https://github.com/yasukata/fmalloc> and custom allocator concepts from
<https://gist.github.com/s-u/6712c97ca74181f5a1a5>.

## License

This project is licensed under GPL (\>= 2).
