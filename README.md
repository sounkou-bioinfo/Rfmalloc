
# Rfmalloc

Rfmalloc provides persistent custom R memory allocation using
the fmalloc library. It was supposed to offers large file-backed memory allocation with full
malloc, free, and realloc support for efficient persistent storage. But there appear to be an issue with the chunk allocation and system mmap fallback for very large R vectors.
So i would not recommend using this if you are not just allocating a bunch of small vectors. Plus `Rf_allocVector3` is non [api](https://github.com/r-devel/r-svn/blob/b8ffe27b6b430f67b20518071f018f07bff00f4d/src/include/R_ext/Rallocators.h#L27) . Just use mmamp like Simon Urbanek does in the gist in the references, or just forget about this given non api status. [bettermc](https://github.com/akersting/bettermc) used to be the only mainline R package that uses Rf_allocVector3 though [others are cargo, luajr, profmem and MonetDBLite](https://github.com/search?q=org%3Acran%20allocvector3&type=code)

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
#> fmalloc initialized with file: /tmp/RtmpK0ytnS/filee1e019c970c8.bin (init: true)
#> [1] TRUE

# Create vectors using file-backed allocation
v_int <- create_fmalloc_vector("integer", 10)
v_num <- create_fmalloc_vector("numeric", 10)

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
#>          used (Mb) gc trigger (Mb) max used (Mb)
#> Ncells 522110 27.9    1133034 60.6   717417 38.4
#> Vcells 986132  7.6    8388608 64.0  2021519 15.5
cleanup_fmalloc()
#> Cleaning up fmalloc...
#> fmalloc cleaned up
file.remove(alloc_file)
#> [1] TRUE
```

## Largish Examples

``` r
library(Rfmalloc)

# Create a 100MB backing file
large_file <- tempfile(fileext = ".bin")
init_fmalloc(large_file, size_gb = 0.1)
#> Requested file size: 0.10 GB (107374182 bytes)
#> Creating file with size: 107374182 bytes (0.10 GB)
#> fmalloc initialized with file: /tmp/RtmpK0ytnS/filee1e032303134.bin (init: true)
#> [1] TRUE

# Create larger vectors
vec1 <- create_fmalloc_vector("integer", 1000)
#> Creating fmalloc vector: type=13, length=1000
#> Successfully created fmalloc vector
vec2 <- create_fmalloc_vector("numeric", 500)

# Fill with data
vec1[1:10] <- 1:10
vec2[1:10] <- (1:10) * 1.5

cat("Sample data:", vec1[1:5], "\n")
#> Sample data: 1 2 3 4 5
cat("Random access:", vec1[sample(10, 3)], "\n")
#> Random access: 3 9 5

# Clean up - force garbage collection before cleanup to avoid warnings
rm(vec1, vec2)
gc()
#>          used (Mb) gc trigger (Mb) max used (Mb)
#> Ncells 522715 28.0    1133034 60.6   717417 38.4
#> Vcells 987936  7.6    8388608 64.0  2021519 15.5
cleanup_fmalloc()
#> Cleaning up fmalloc...
#> fmalloc cleaned up
file.remove(large_file)
#> [1] TRUE
```

## References

This exploratory project used the fmalloc library from <https://github.com/yasukata/fmalloc> and a mmap custom allocator example from Simon Urbanek at this gist <https://gist.github.com/s-u/6712c97ca74181f5a1a5>.

