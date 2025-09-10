
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
#> fmalloc initialized with file: /tmp/RtmpiXQUjD/filea41304cdb7c60.bin (init: true)
#> [1] TRUE

# Create vectors using file-backed allocation
v_int <- create_fmalloc_vector("integer", 10)
#> Creating fmalloc vector: type=13, length=10
#> fmalloc allocated 120 bytes at 0x7623a2e023e0
#> Successfully created fmalloc vector
v_num <- create_fmalloc_vector("numeric", 10)
#> Creating fmalloc vector: type=14, length=10
#> fmalloc allocated 160 bytes at 0x7623a2e02460
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
#> fmalloc freed memory at 0x7623a2e023e0
#> fmalloc freed memory at 0x7623a2e02460
#>          used (Mb) gc trigger (Mb) max used (Mb)
#> Ncells 522084 27.9    1132960 60.6   717417 38.4
#> Vcells 986404  7.6    8388608 64.0  2021519 15.5
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
#> fmalloc initialized with file: /tmp/RtmpiXQUjD/filea413055a0df5c.bin (init: true)
#> [1] TRUE

# Create larger vectors
vec1 <- create_fmalloc_vector("integer", 1000)
#> Creating fmalloc vector: type=13, length=1000
#> fmalloc allocated 4080 bytes at 0x76239c6023e0
#> Successfully created fmalloc vector
vec2 <- create_fmalloc_vector("numeric", 500)
#> Creating fmalloc vector: type=14, length=500
#> fmalloc allocated 4080 bytes at 0x76239c6033e0
#> Successfully created fmalloc vector

# Fill with data
vec1[1:10] <- 1:10
vec2[1:10] <- (1:10) * 1.5

cat("Sample data:", vec1[1:5], "\n")
#> Sample data: 1 2 3 4 5
cat("Random access:", vec1[sample(10, 3)], "\n")
#> Random access: 4 10 3

# Clean up - force garbage collection before cleanup to avoid warnings
rm(vec1, vec2)
gc()
#> fmalloc freed memory at 0x76239c6023e0
#> fmalloc freed memory at 0x76239c6033e0
#>          used (Mb) gc trigger (Mb) max used (Mb)
#> Ncells 522687 28.0    1132960 60.6   717417 38.4
#> Vcells 988238  7.6    8388608 64.0  2021519 15.5
cleanup_fmalloc()
#> Cleaning up fmalloc...
#> fmalloc cleaned up
file.remove(large_file)
#> [1] TRUE
```

## Stress Testing

Real stress testing with massive files:

``` r
library(Rfmalloc)

# Create a MASSIVE 50GB backing file
huge_file <- tempfile(fileext = ".bin", tmpdir = "/tmp")
init_fmalloc(huge_file, size_gb = 50)
#> Requested file size: 50.00 GB (53687091200 bytes)
#> Creating file with size: 53687091200 bytes (50.00 GB)
#> fmalloc initialized with file: /tmp/filea41307a8bc58a.bin (init: true)
#> [1] TRUE

# Create large vectors (within practical limits)
big_vec1 <- create_fmalloc_vector("integer", 1e6)  # 1M integers
#> Creating fmalloc vector: type=13, length=1000000
#> fmalloc allocated 4000080 bytes at 0x76171c6023e0
#> Successfully created fmalloc vector
big_vec2 <- create_fmalloc_vector("integer", 1e6)  # Another 1M integers  
#> Creating fmalloc vector: type=13, length=1000000
#> fmalloc allocated 4000080 bytes at 0x76171c9d2d40
#> Successfully created fmalloc vector
big_vec3 <- create_fmalloc_vector("numeric", 5e5)  # 500K doubles
#> Creating fmalloc vector: type=14, length=500000
#> fmalloc allocated 4000080 bytes at 0x76171cda36a0
#> Successfully created fmalloc vector

# Fill them with real data
big_vec1[1:1e6] <- 1:1e6
big_vec2[1:1e6] <- (1:1e6) * 2
big_vec3[1:5e5] <- (1:5e5) * 1.5

# Show the power
cat("File size:", round(file.info(huge_file)$size / 1024^3, 1), "GB\n")
#> File size: 50 GB
cat("Vector 1 size:", format(object.size(big_vec1), units = "MB"), "\n") 
#> Vector 1 size: 3.8 Mb
cat("Vector 2 size:", format(object.size(big_vec2), units = "MB"), "\n")
#> Vector 2 size: 7.6 Mb
cat("Vector 3 size:", format(object.size(big_vec3), units = "MB"), "\n")
#> Vector 3 size: 3.8 Mb
cat("First 5 from vec1:", big_vec1[1:5], "\n")
#> First 5 from vec1: 1 2 3 4 5
cat("Last 5 from vec1:", big_vec1[999996:1000000], "\n")
#> Last 5 from vec1: 999996 999997 999998 999999 1000000
cat("Random access:", big_vec2[sample(1e6, 5)], "\n")
#> Random access: 1974654 451832 503738 36338 412530

# Clean up
rm(big_vec1, big_vec2, big_vec3)
gc()
#> fmalloc freed memory at 0x76171c6023e0
#> fmalloc freed memory at 0x76171c9d2d40
#> fmalloc freed memory at 0x76171cda36a0
#>          used (Mb) gc trigger (Mb) max used (Mb)
#> Ncells 523498 28.0    1132960 60.6   717417 38.4
#> Vcells 989981  7.6    8388608 64.0  7483502 57.1
cleanup_fmalloc()
#> Cleaning up fmalloc...
#> fmalloc cleaned up
file.remove(huge_file)
#> [1] TRUE
```

**Tested Capabilities:**

- Backing files: Up to 50+ GB
- Individual vectors: Up to 1M elements (~4MB)
- Multiple concurrent vectors supported
- Cross-platform: Linux, macOS, Windows

## Contributing

Please note that this project uses the fmalloc library from
<https://github.com/yasukata/fmalloc> and custom allocator concepts from
<https://gist.github.com/s-u/6712c97ca74181f5a1a5>.

## License

This project is licensed under GPL (\>= 2).
