
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
#> fmalloc initialized with file: /tmp/RtmpDAQ653/file9dca691c300f.bin (init: true)
#> [1] TRUE

# Create vectors using file-backed allocation
v_int <- create_fmalloc_vector("integer", 10)
#> Creating fmalloc vector: type=13, length=10
#> fmalloc allocated 120 bytes at 0x712dc28023e0
#> Successfully created fmalloc vector
v_num <- create_fmalloc_vector("numeric", 10)
#> Creating fmalloc vector: type=14, length=10
#> fmalloc allocated 160 bytes at 0x712dc2802460
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

## Contributing

Please note that this project uses the fmalloc library from
<https://github.com/yasukata/fmalloc> and custom allocator concepts from
<https://gist.github.com/s-u/6712c97ca74181f5a1a5>.

## License

This project is licensed under GPL (\>= 2).
