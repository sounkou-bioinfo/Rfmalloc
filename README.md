
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

You can install the development version of Rfmalloc from
[GitHub](https://github.com/) with:

``` r
# install.packages("devtools")
devtools::install_github("sounkou-bioinfo/Rfmalloc")
```

## System Requirements

- C++11 compiler
- POSIX-compliant system (Linux, macOS) or Windows
- pthreads library (on POSIX systems)

## Basic Usage

``` r
library(Rfmalloc)

# Initialize fmalloc with a backing file
alloc_file <- tempfile(fileext = ".bin")
init_fmalloc(alloc_file)
#> Creating file with size: 33554432 bytes (0.03 GB)
#> fmalloc initialized with file: /tmp/RtmpUrRxu8/file9ff091f81a174.bin (init: true)
#> [1] TRUE

# Create vectors using file-backed allocation
v_int <- create_fmalloc_vector("integer", 10)
#> Creating fmalloc vector: type=13, length=10
#> fmalloc allocated 120 bytes at 0x7b5ae9c023e0
#> Successfully created fmalloc vector
v_num <- create_fmalloc_vector("numeric", 10)
#> Creating fmalloc vector: type=14, length=10
#> fmalloc allocated 160 bytes at 0x7b5ae9c02460
#> Successfully created fmalloc vector

# Use the vectors normally
v_int[1:3] <- c(1L, 2L, 3L)
v_num[1:3] <- c(1.1, 2.2, 3.3)

print(v_int[1:3])
#> [1] 1 2 3
print(v_num[1:3])
#> [1] 1.1 2.2 3.3

# Clean up
cleanup_fmalloc()
#> Cleaning up fmalloc...
#> fmalloc cleaned up
file.remove(alloc_file)
#> [1] TRUE
```

## Stress Testing with Large Files

For stress testing with very large files and vectors:

``` r
library(Rfmalloc)

# Create a very large backing file (50 GB)
large_file <- tempfile(fileext = ".bin", tmpdir = "/tmp")  # Use /tmp for large files

# Initialize with 50 GB file
init_fmalloc(large_file, size_gb = 50)

# Create very large vectors (e.g., 1 billion integers = ~4GB)
big_int_vector <- create_fmalloc_vector("integer", 1e9)
big_num_vector <- create_fmalloc_vector("numeric", 5e8)  # 500M doubles = ~4GB

# Fill with some data
cat("Filling large vectors...\n")
big_int_vector[1:1000] <- 1:1000
big_num_vector[1:1000] <- (1:1000) * 1.5

# Verify data persistence
cat("First 10 integers:", big_int_vector[1:10], "\n")
cat("First 10 numerics:", big_num_vector[1:10], "\n")

# Memory usage info
cat("Vector sizes:\n")
cat("Integer vector: ", object.size(big_int_vector), "bytes\n")
cat("Numeric vector: ", object.size(big_num_vector), "bytes\n")

# Clean up
cleanup_fmalloc()
file.remove(large_file)
```

### Running the Comprehensive Stress Test

A comprehensive stress test script is provided that demonstrates the
full capabilities:

``` r
# Run the built-in stress test
source(system.file("stress_test.R", package = "Rfmalloc"))
```

This stress test will:

- Create a 50 GB backing file
- Allocate vectors totaling ~8 GB in memory
- Test random access patterns
- Measure performance metrics
- Verify data integrity
- Clean up resources automatically

**Performance Expectations:**

- **File Creation**: Large files are created using sparse allocation
  (fast)
- **Vector Allocation**: Memory mapping provides near-instant allocation
- **Data Access**: Random access performance similar to regular R
  vectors
- **Memory Efficiency**: Only allocated regions consume actual disk
  space

**System Requirements for Stress Testing:**

- At least 60 GB available disk space
- 8+ GB RAM recommended
- Modern SSD for best performance
- POSIX-compliant system (Linux/macOS) or Windows

## Technical Details

The fmalloc allocator provides:

- **File-backed allocation** - Memory allocated from memory-mapped files
  instead of heap
- **Full realloc support** - Complete malloc/free/realloc patterns via
  dlrealloc()  
- **Persistent storage** - Data stored in files survives process
  restarts
- **Efficient algorithms** - Based on proven ptmalloc3 memory management

## Error Handling

The package includes comprehensive error handling for:

- Invalid file paths and permissions
- Memory allocation failures  
- Type validation
- Resource cleanup

## Performance Testing

To validate the package can handle large-scale workloads, a
comprehensive stress test is included:

``` r
# Run a demo stress test (safe for most systems - uses 1GB)
source(system.file("demo_stress_test.R", package = "Rfmalloc"))

# Run the full stress test (requires ~60GB disk space)
source(system.file("stress_test.R", package = "Rfmalloc"))
```

The **demo stress test** creates a 1GB backing file and demonstrates
large vector operations safely.

The **full stress test** creates a 50GB backing file and allocates
multiple gigabyte-sized vectors to verify system stability and
performance under extreme load.

## Contributing

Please note that this project uses the fmalloc library from
<https://github.com/yasukata/fmalloc> and custom allocator concepts from
<https://gist.github.com/s-u/6712c97ca74181f5a1a5>.

## License

This project is licensed under GPL (\>= 2).
