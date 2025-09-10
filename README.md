
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
- POSIX-compliant system (Linux, macOS)
- pthreads library

## Basic Usage

### File-backed Allocation with Realloc Support

Use fmalloc for persistent memory allocation with full
malloc/free/realloc support:

``` r
library(Rfmalloc)

# Initialize fmalloc with a backing file
alloc_file <- tempfile(fileext = ".bin")
init_result <- init_fmalloc(alloc_file)
#> fmalloc initialized with file: /tmp/RtmpE2RfNO/file8c68e5b8ca98a.bin (init: true)
print(paste("Initialization result:", init_result))
#> [1] "Initialization result: TRUE"

# Create vectors using fmalloc
v_int <- create_fmalloc_vector("integer", 10)
#> Creating fmalloc vector: type=13, length=10
#> fmalloc allocated 120 bytes at 0x73cdc54023e0
#> Successfully created fmalloc vector
v_num <- create_fmalloc_vector("numeric", 10)
#> Creating fmalloc vector: type=14, length=10
#> fmalloc allocated 160 bytes at 0x73cdc5402460
#> Successfully created fmalloc vector

# Simple assignment (no reallocation)
v_int[1:3] <- c(1L, 2L, 3L)
v_num[1:3] <- c(1.1, 2.2, 3.3)

print("Integer vector:")
#> [1] "Integer vector:"
print(v_int[1:3])
#> [1] 1 2 3
print("Numeric vector:")
#> [1] "Numeric vector:"
print(v_num[1:3])
#> [1] 1.1 2.2 3.3

# Clean up
cleanup_fmalloc()
#> Cleaning up fmalloc...
#> fmalloc cleaned up

# Remove temporary file
if (file.exists(alloc_file)) {
  file.remove(alloc_file)
}
#> [1] TRUE
```

## Technical Details

### fmalloc Implementation

The fmalloc allocator provides:

- **File-backed allocation** - Memory allocated from memory-mapped files
  instead of heap
- **Full realloc support** - Complete malloc/free/realloc patterns via
  dlrealloc()  
- **Persistent storage** - Data stored in files survives process
  restarts
- **Efficient algorithms** - Based on proven ptmalloc3 memory management
- **Memory-mapped regions** - Direct file-to-memory mapping for
  performance

### Realloc Support

fmalloc includes full realloc support through the `dlrealloc()`
function:

- **Memory expansion** - Grow allocated regions efficiently
- **Memory shrinking** - Reduce allocation size when needed
- **Copy optimization** - Minimizes data copying during reallocation
- **R integration** - Seamlessly works with R’s internal memory
  management

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
