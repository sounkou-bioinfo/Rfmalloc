# Rgguf

Rgguf reads [GGUF](https://github.com/ggerganov/ggml/blob/master/docs/gguf.md)
model files — the format used by [llama.cpp](https://github.com/ggerganov/llama.cpp)
to store large language model tensors and metadata — and exposes their
tensors directly as [Rfmalloc](https://github.com/sounkou-bioinfo/Rfmalloc)-backed,
file-backed ALTREP matrices and arrays, dequantizing quantized tensor types
on demand as they are read.

GGUF parsing is done by a vendored, MIT-licensed copy of Salvatore
Sanfilippo's (antirez's) [gguflib](https://github.com/antirez/gguf-tools)
(`src/gguflib.c`, `fp16.c`, `bf16.h`); see `inst/COPYRIGHTS` for full
attribution.

## Design

Allocation happens on the R side, filling happens in C:

1. R allocates the destination with
   `Rfmalloc::create_fmalloc_matrix()`/`create_fmalloc_array()`, which
   returns a properly classed, file-backed ALTREP object — `Rfmalloc`'s
   `Ops`/matrix-product dispatch already works on it.
2. Native code locates the requested tensor in the memory-mapped GGUF file
   and writes dequantized values directly into the destination's `REAL()`
   payload (the ALTREP `Dataptr` exposes the file-backed `fmalloc` memory
   directly — no extra copy back into an ordinary R vector).

GGML/GGUF tensors store `dim[0]` as the fastest-varying (contiguous)
dimension. R arrays are column-major (the first dimension varies fastest),
so `dim[0]` maps directly onto R's first dimension: `dim(gguf_tensor(x,
name)) == c(dim[0], dim[1], ...)`, with no transposition needed. This is
verified by a dedicated round-trip test
(`inst/tinytest/test_gguf_roundtrip.R`).

## Installation

```r
# Rfmalloc must already be installed (Rgguf links to and imports it).
install.packages("Rgguf", repos = NULL, type = "source")
```

## Usage

```r
library(Rgguf)

# Build a small example GGUF file (gguf_write_tensors() only emits F32
# tensors; it exists mainly to make round-trip tests possible without
# shipping binary fixtures).
tmp <- tempfile(fileext = ".gguf")
gguf_write_tensors(tmp,
    tensors = list(
        w1 = matrix(rnorm(100 * 50), nrow = 100, ncol = 50),
        w2 = matrix(rnorm(50 * 30), nrow = 50, ncol = 30)
    ),
    metadata = list(name = "example-model", version = 1)
)

# Inspect metadata and the tensor directory without reading any payload.
gguf_metadata(tmp)
gguf_tensors(tmp)

# Read tensors into Rfmalloc-backed matrices sharing one backing file.
rt <- Rfmalloc::open_fmalloc(tempfile(fileext = ".bin"))
mats <- gguf_import(tmp, runtime = rt)

Rfmalloc::is_fmalloc_vector(mats$w1)
#> [1] TRUE

product <- mats$w1 %*% mats$w2
Rfmalloc::is_fmalloc_vector(product)
#> [1] TRUE
```

## Known limitation: `%*%`/`crossprod`/`tcrossprod` on large operands

`Rfmalloc` 0.1.0 has a limitation, discovered while building this package,
that affects any fmalloc-backed vector/matrix with 64 or more elements (not
just ones produced by Rgguf): `%*%.fmalloc()`/`crossprod.fmalloc()`/
`tcrossprod.fmalloc()` strip the `"fmalloc"` S3 class from their operands
with `class(x) <- NULL` before dispatching to native code. For a
*referenced* ALTREP object (any argument passed through a closure call, which
is unavoidable here) of length >= 64, R's own attribute-assignment machinery
replaces the object with a generic ALTREP "wrapper" instead of invoking the
class's registered `Duplicate` method - this reproduces even for base R's
own compact `1:100` ALTREP sequences, so it is general R behavior, not
something specific to `Rfmalloc`'s ALTREP class. `Rfmalloc`'s internal
`maybe_vector_from_altrep()` does not unwrap that generic wrapper, so the
runtime cannot be found and the call fails with `"fmalloc matrix operation
requires an fmalloc runtime"`.

This is a dependency-level issue, reproducible with pure `Rfmalloc` code and
no `Rgguf` involved at all; see `inst/smoke_test.R` for a full write-up and a
demonstration that the same workflow succeeds end-to-end below that
threshold. `gguf_tensor()`/`gguf_import()` themselves are unaffected - the
tensors they return are correctly dequantized and genuinely `Rfmalloc`-backed
at any size.

## Supported tensor types

`gguf_tensor()`/`gguf_import()` dequantize:

- Direct/streamed (no intermediate buffer): `f32`, `f16`, `bf16`, `f64`,
  `i8`, `i16`, `i32`, `i64`.
- Via the vendored `gguflib`'s own `gguf_tensor_to_float()` (through a
  transient `malloc()`'d float buffer, freed immediately after widening to
  double — a known v1 memory cost): `q8_0`, `q4_0`, `q4_1`, `q2_k`, `q4_k`,
  `q6_k`.

Other quantized formats (`q5_0`, `q5_1`, `q8_1`, `q3_k`, `q5_k`, `q8_k`, and
the `iq*` formats) are not implemented by the vendored parser; `gguf_tensor()`
errors clearly instead of guessing at them.

## Note on `gguf_open()`

The vendored parser memory-maps GGUF files read/write
(`mmap(..., PROT_READ|PROT_WRITE, MAP_SHARED, ...)`), so `gguf_open()`
requires the file to be writable even when you only intend to read tensors
from it.

## License

MIT (see `LICENSE`). Bundled third-party code is documented and separately
attributed in `inst/COPYRIGHTS`.
