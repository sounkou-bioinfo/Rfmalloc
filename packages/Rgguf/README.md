# Rgguf

<!-- badges: start -->

[![R-CMD-check](https://github.com/sounkou-bioinfo/Rgguf/actions/workflows/R-CMD-check.yaml/badge.svg)](https://github.com/sounkou-bioinfo/Rgguf/actions/workflows/R-CMD-check.yaml)
<!-- badges: end -->

Rgguf reads
[GGUF](https://github.com/ggerganov/ggml/blob/master/docs/gguf.md) model
files — the format used by
[llama.cpp](https://github.com/ggerganov/llama.cpp) to store large
language model tensors and metadata — and exposes their tensors directly
as [Rfmalloc](https://github.com/sounkou-bioinfo/Rfmalloc)-backed,
file-backed ALTREP matrices and arrays, dequantizing quantized tensor
types on demand as they are read.

GGUF parsing is done by a vendored, MIT-licensed copy of Salvatore
Sanfilippo’s (antirez’s)
[gguflib](https://github.com/antirez/gguf-tools) (`src/gguflib.c`,
`fp16.c`, `bf16.h`); see `inst/COPYRIGHTS` for full attribution.

## Design

Allocation happens on the R side, filling happens in C:

1.  R allocates the destination with
    `Rfmalloc::create_fmalloc_matrix()`/`create_fmalloc_array()`, which
    returns a properly classed, file-backed ALTREP object — `Rfmalloc`’s
    `Ops`/matrix-product dispatch already works on it.
2.  Native code locates the requested tensor in the memory-mapped GGUF
    file and writes dequantized values directly into the destination’s
    `REAL()` payload (the ALTREP `Dataptr` exposes the file-backed
    `fmalloc` memory directly — no extra copy back into an ordinary R
    vector).

GGML/GGUF tensors store `dim[0]` as the fastest-varying (contiguous)
dimension. R arrays are column-major (the first dimension varies
fastest), so `dim[0]` maps directly onto R’s first dimension:
`dim(gguf_tensor(x, name)) == c(dim[0], dim[1], ...)`, with no
transposition needed. This is verified by a dedicated round-trip test
(`inst/tinytest/test_gguf_roundtrip.R`).

## Installation

``` r
# install.packages("remotes")
remotes::install_github("sounkou-bioinfo/Rfmalloc")
remotes::install_github("sounkou-bioinfo/Rgguf")
```

## Usage

The example runs inside `local()` so its `on.exit()` cleanup stays
scoped to the snippet while the README is rendered; in your own code,
keep the runtime handle you need and call `Rfmalloc::cleanup_fmalloc()`
when finished.

``` r
library(Rgguf)

local({
  gguf_file <- tempfile(fileext = ".gguf")
  alloc_file <- tempfile(fileext = ".bin")
  rt <- Rfmalloc::open_fmalloc(alloc_file)
  on.exit({
    Rfmalloc::cleanup_fmalloc(rt)
    unlink(c(gguf_file, alloc_file))
  }, add = TRUE)

  # Build a small example GGUF file (gguf_write_tensors() only emits F32
  # tensors; it exists mainly to make round-trip tests possible without
  # shipping binary fixtures).
  set.seed(42)
  gguf_write_tensors(gguf_file,
    tensors = list(
      w1 = matrix(rnorm(100 * 50), nrow = 100, ncol = 50),
      w2 = matrix(rnorm(50 * 30), nrow = 50, ncol = 30)
    ),
    metadata = list(name = "example-model", version = 1)
  )

  # Inspect metadata and the tensor directory without reading any payload.
  str(gguf_metadata(gguf_file))
  print(gguf_tensors(gguf_file))

  # Read tensors into Rfmalloc-backed matrices sharing one backing file.
  mats <- gguf_import(gguf_file, runtime = rt)
  print(Rfmalloc::is_fmalloc_vector(mats$w1))

  # Matrix products dispatch through Rfmalloc and stay file-backed.
  product <- mats$w1 %*% mats$w2
  print(dim(product))
  print(Rfmalloc::is_fmalloc_vector(product))
  print(max(abs(product - as.matrix(mats$w1[, ]) %*% as.matrix(mats$w2[, ]))))
})
#> List of 2
#>  $ name   : chr "example-model"
#>  $ version: num 1
#>   name type n_dims    dims n_elements nbytes offset
#> 1   w1  f32      2 100, 50       5000  20000    192
#> 2   w2  f32      2  50, 30       1500   6000  20192
#> [1] TRUE
#> [1] 100  30
#> [1] TRUE
#> [1] 0
```

## Native typed tensors

`gguf_tensor(as = "native")` skips dequantization at import: the
tensor’s raw GGUF payload is copied into fmalloc storage at its original
density (4.5 bits/weight for `q4_k`) and returned as an
`Rfmalloc::fmalloc_tensor`. Matrix products against dense operands
decode the payload in bounded, block-aligned panels streamed through
BLAS `dgemm`, so the full double representation is never materialized.
Rgguf registers gguflib’s `q4_0`, `q4_1`, `q8_0`, `q2_k`, `q4_k`, and
`q6_k` dequantizers as Rfmalloc tensor codecs; `f32`, `f16`, `bf16`, and
`f64` codecs are built into Rfmalloc.

``` r
local({
  gguf_file <- tempfile(fileext = ".gguf")
  alloc_file <- tempfile(fileext = ".bin")
  rt <- Rfmalloc::open_fmalloc(alloc_file)
  on.exit({
    Rfmalloc::cleanup_fmalloc(rt)
    unlink(c(gguf_file, alloc_file))
  }, add = TRUE)

  set.seed(7)
  gguf_write_tensors(gguf_file, list(
    w = matrix(rnorm(256 * 64), nrow = 256, ncol = 64)
  ))

  ten <- gguf_tensor(gguf_file, "w", runtime = rt, as = "native")
  print(ten)

  x <- matrix(rnorm(8 * 256), nrow = 8)
  y <- x %*% ten # decoded panel-by-panel inside dgemm
  print(dim(y))
  print(Rfmalloc::is_fmalloc_vector(y))

  w_f64 <- gguf_tensor(gguf_file, "w", runtime = rt)
  print(max(abs(y[] - (x %*% w_f64)[])))
})
#> <fmalloc_tensor f32 [256 x 64], 65536 payload bytes>
#> [1]  8 64
#> [1] TRUE
#> [1] 0
```

## Supported tensor types

`gguf_tensor()`/`gguf_import()` dequantize:

- Direct/streamed (no intermediate buffer): `f32`, `f16`, `bf16`, `f64`,
  `i8`, `i16`, `i32`, `i64`.
- Via the vendored `gguflib`’s own `gguf_tensor_to_float()` (through a
  transient `malloc()`’d float buffer, freed immediately after widening
  to double — a known v1 memory cost): `q8_0`, `q4_0`, `q4_1`, `q2_k`,
  `q4_k`, `q6_k`.

Other quantized formats (`q5_0`, `q5_1`, `q8_1`, `q3_k`, `q5_k`, `q8_k`,
and the `iq*` formats) are not implemented by the vendored parser;
`gguf_tensor()` errors clearly instead of guessing at them.

## Note on `gguf_open()`

The vendored parser memory-maps GGUF files read/write
(`mmap(..., PROT_READ|PROT_WRITE, MAP_SHARED, ...)`), so `gguf_open()`
requires the file to be writable even when you only intend to read
tensors from it.

## License

GPL (\>= 2). Bundled third-party code (gguflib, fp16, bf16) remains
under its upstream MIT license, which is GPL-compatible; it is
documented and separately attributed in `inst/COPYRIGHTS`.
