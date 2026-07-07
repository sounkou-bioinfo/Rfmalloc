# Rfmalloc

Rfmalloc gives you **the real R matrix, file-backed** — a genuine ALTREP
numeric vector/matrix whose storage lives in an `mmap`-ed file instead
of the R heap, powered by a patched copy of
[fmalloc](https://github.com/yasukata/fmalloc). Because it is the actual
R object and not a proxy handle or a lazy file index,
[`dim()`](https://rdrr.io/r/base/dim.html), `%*%`,
[`crossprod()`](https://rdrr.io/r/base/crossprod.html), `Ops`, and
subsetting just work — but the data can be larger than RAM, compressed
on disk, mutated in place, and multiplied by a pluggable backend.

The direction of the project is an **out-of-core, compressed,
in-place-mutable matrix stack** built from three runtime-pluggable tiers
over fmalloc storage:

- **codecs** — *how bytes decode*: builtin `f32`/`f16`/`bf16`, a
  lossless floating-point codec (`alp`), a `sparse` codec for
  mostly-zero data (single-cell counts), and quantized GGUF formats via
  [Rgguf](https://github.com/sounkou-bioinfo/Rgguf) — all registered
  through a codec registry other packages can extend;
- **backends** — *what hardware multiplies*: the matrix products
  dispatch `dgemm` through a selectable backend (default R’s BLAS;
  downstream packages can register a GPU or out-of-core-aware kernel);
- **fmalloc** — *where it lives*: file-backed, out-of-core, persistent,
  and mutable in place.

Contrast with neighbours: this is not a lazy file index
([vroom](https://vroom.r-lib.org)) and not a proxy class
([bigmemory](https://cran.r-project.org/package=bigmemory),
[bigstatsr](https://privefl.github.io/bigstatsr/) FBM,
[houba](https://github.com/HervePerdry/houba)) — it is the real ALTREP
object that computes and compresses out of core.

Earlier prototypes explored R’s custom allocator / `Rf_allocVector3()`
path (see Simon Urbanek’s
[proof-of-concept](https://gist.github.com/s-u/6712c97ca74181f5a1a5)).
The patched fmalloc `malloc`/`free`/`realloc` layer is still built and
installed for native consumers that use the C API or link against the
bundled library.

## Current Status

Storage and vectors:

- fmalloc-backed ALTREP vectors for logical, integer, numeric, raw,
  complex, character, and list values; explicit runtime handles and
  `persistent` / `scratch` modes; persistent serialization/reopening for
  fixed-width atomic and character vectors.

Matrix computing:

- native elementwise `Ops`, `Summary`, `Math`/`Math2`, and matrix
  reductions;
- matrix products (`%*%`,
  [`crossprod()`](https://rdrr.io/r/base/crossprod.html),
  [`tcrossprod()`](https://rdrr.io/r/base/crossprod.html)) call BLAS
  `dgemm` for finite double operands (managed native loops otherwise),
  dispatched through a **pluggable backend**
  ([`fmalloc_matmul_backend()`](https://sounkou-bioinfo.github.io/Rfmalloc/reference/fmalloc_backend.md));
- **out-of-core** products
  ([`fmalloc_matmul_ooc()`](https://sounkou-bioinfo.github.io/Rfmalloc/reference/fmalloc_matmul_ooc.md),
  [`fmalloc_crossprod_ooc()`](https://sounkou-bioinfo.github.io/Rfmalloc/reference/fmalloc_matmul_ooc.md),
  [`fmalloc_tcrossprod_ooc()`](https://sounkou-bioinfo.github.io/Rfmalloc/reference/fmalloc_matmul_ooc.md))
  that stream column tiles through `dgemm` with `madvise` eviction, so
  operands larger than RAM multiply with a bounded resident set;
  `%*%`/`crossprod`/`tcrossprod` auto-route to these above a RAM-based
  threshold.

Compression (typed tensors):

- [`as_fmalloc_tensor()`](https://sounkou-bioinfo.github.io/Rfmalloc/reference/fmalloc_tensor.md)
  stores a matrix under a codec — lossless `"alp"` for decimal-scaled
  doubles, `"sparse"` for mostly-zero (single-cell/count) data — and the
  compressed tensor still participates in the panel-streamed matrix
  products;
- codecs are a registry
  ([`fmalloc_tensor_codecs()`](https://sounkou-bioinfo.github.io/Rfmalloc/reference/fmalloc_tensor.md));
  downstream packages add their own (Rgguf registers the quantized GGUF
  formats).

In-place mutation:

- [`fmalloc_set()`](https://sounkou-bioinfo.github.io/Rfmalloc/reference/fmalloc_insitu.md),
  [`fmalloc_fill()`](https://sounkou-bioinfo.github.io/Rfmalloc/reference/fmalloc_insitu.md),
  and
  [`fmalloc_add()`](https://sounkou-bioinfo.github.io/Rfmalloc/reference/fmalloc_insitu.md)/[`sub()`](https://rdrr.io/r/base/grep.html)/`mul()`/`div()`
  write through the backing store, bypassing R’s copy-on-modify —
  essential when the payload is larger than RAM or persistent.

Native surface:

- explicit
  [`destroy_fmalloc_vector()`](https://sounkou-bioinfo.github.io/Rfmalloc/reference/destroy_fmalloc_vector.md),
  an in-file persistent allocation catalog, runtime/vector
  introspection, and an installed C header with `R_RegisterCCallable()`
  entry points (API version 5), including tensor codec and matmul
  backend registration for downstream packages.

Known base-fallback boundaries:

- [`rowSums()`](https://sounkou-bioinfo.github.io/Rfmalloc/reference/fmalloc_reduction_methods.md)/[`colSums()`](https://sounkou-bioinfo.github.io/Rfmalloc/reference/fmalloc_reduction_methods.md)/[`rowMeans()`](https://sounkou-bioinfo.github.io/Rfmalloc/reference/fmalloc_reduction_methods.md)/[`colMeans()`](https://sounkou-bioinfo.github.io/Rfmalloc/reference/fmalloc_reduction_methods.md)
  fall back to base (with a warning) when the input is not an exact 2D
  matrix or `dims != 1L`;
- scalar or zero-length results from `Summary`, `Math`, and `Math2` are
  ordinary R scalars by design;
- matrix products fall back to managed loops for `NA`/`NaN`/`Inf` and
  logical/integer/complex operands, matching base semantics.

## Installation

``` r

# install.packages("devtools")
devtools::install_github("sounkou-bioinfo/Rfmalloc")
```

## Basic Usage

The examples use temporary backing files. Each snippet runs inside
[`local()`](https://rdrr.io/r/base/eval.html) so its
[`on.exit()`](https://rdrr.io/r/base/on.exit.html) cleanup stays scoped
to the snippet while the README is rendered; this is not required in
normal interactive use. In your own code, keep the runtime handle you
need and call
[`cleanup_fmalloc()`](https://sounkou-bioinfo.github.io/Rfmalloc/reference/cleanup_fmalloc.md)
when finished.

The shortest form uses
[`init_fmalloc()`](https://sounkou-bioinfo.github.io/Rfmalloc/reference/init_fmalloc.md),
which opens a backing file and stores it as the package default runtime.
Calls to
[`create_fmalloc_vector()`](https://sounkou-bioinfo.github.io/Rfmalloc/reference/create_fmalloc_vector.md)
can then omit the `runtime` argument:

``` r

library(Rfmalloc)
local({
  alloc_file <- tempfile(fileext = ".bin")
  init_fmalloc(alloc_file)
  on.exit({
    cleanup_fmalloc()
    unlink(alloc_file)
  }, add = TRUE)

  v_int <- create_fmalloc_vector("integer", 10)
  v_num <- create_fmalloc_vector("numeric", 10)
  v_raw <- create_fmalloc_vector("raw", 4)
  v_cplx <- create_fmalloc_vector("complex", 4)
  v_chr <- create_fmalloc_vector("character", 3)
  v_lst <- create_fmalloc_vector("list", 2)

  v_int[1:3] <- c(1L, 2L, 3L)
  v_num[1:3] <- c(1.1, 2.2, 3.3)
  v_raw[] <- as.raw(1:4)
  v_cplx[] <- c(1+1i, 2+2i, 3+3i, 4+4i)
  v_chr[] <- c("alpha", "beta", NA_character_)

  list_child <- create_fmalloc_vector("integer", 2)
  list_child[] <- 1:2
  v_lst[[1]] <- list_child

  list(
    integer = v_int[1:3],
    numeric = v_num[1:3],
    raw = v_raw[],
    complex = v_cplx[],
    character = v_chr[],
    list_first = v_lst[[1]][]
  )
})
#> $integer
#> [1] 1 2 3
#> 
#> $numeric
#> [1] 1.1 2.2 3.3
#> 
#> $raw
#> [1] 01 02 03 04
#> 
#> $complex
#> [1] 1+1i 2+2i 3+3i 4+4i
#> 
#> $character
#> [1] "alpha" "beta"  NA     
#> 
#> $list_first
#> [1] 1 2
```

For multiple backing files or explicit lifetime management, prefer
runtime handles returned by
[`open_fmalloc()`](https://sounkou-bioinfo.github.io/Rfmalloc/reference/open_fmalloc.md):

``` r


local({
  handle_file <- tempfile(fileext = ".bin")
  rt <- open_fmalloc(handle_file)
  on.exit({
    cleanup_fmalloc(rt)
    unlink(handle_file)
  }, add = TRUE)

  v <- create_fmalloc_vector("integer", 10, runtime = rt)
  v[1:3] <- 10:12
  v[1:3]
})
#> [1] 10 11 12
```

## Larger Allocation Example

This example is executed when building the README and uses a 1B-length
fmalloc payload.

``` r

local({
  large_file <- tempfile(fileext = ".bin")
  rt <- open_fmalloc(large_file, size_gb = 10)
  on.exit({
    cleanup_fmalloc(rt)
    unlink(large_file)
  }, add = TRUE)

  n <- 10000000L
  before_alloc <- nrow(list_fmalloc_allocations(rt))

  big_int_a <- create_fmalloc_vector("integer", n, runtime = rt)
  big_int_b <- create_fmalloc_vector("integer", n, runtime = rt)
  big_int_a[1:5] <- 1:5
  big_int_b[1:5] <- 6:10

  # Elementwise arithmetic stays on fmalloc-managed ALTREP for the full-length result
  big_sum <- big_int_a + big_int_b
  after_alloc <- nrow(list_fmalloc_allocations(rt))

  list(
    a_head = big_int_a[1:5],
    b_head = big_int_b[1:5],
    sum_head = big_sum[1:5],
    sum_length = length(big_sum),
    sum_managed = inherits(big_sum, "fmalloc") &&
      grepl("fmalloc_altrep", capture.output(.Internal(inspect(big_sum)))[[1L]], fixed = TRUE),
    allocations_delta = after_alloc - before_alloc
  )

  # sum(big_sum) is not used here: Summary(S) generics return scalars.
})
#> $a_head
#> [1] 1 2 3 4 5
#> 
#> $b_head
#> [1]  6  7  8  9 10
#> 
#> $sum_head
#> [1]  7  9 11 13 15
#> 
#> $sum_length
#> [1] 10000000
#> 
#> $sum_managed
#> [1] TRUE
#> 
#> $allocations_delta
#> [1] 3
```

## Arithmetic and comparison operations

Elementwise `Ops` (`+`, `-`, `*`, `/`, `^`, `%%`, `%/%`, `==`, `!=`,
`<`, `>`, `<=`, `>=`, `&`, `|`, `!`, unary `-` and `+`) on fmalloc
vectors are implemented as native C/C++ kernels. Results stay in the
same fmalloc runtime for large vectors.

``` r

local({
  ops_file <- tempfile(fileext = ".bin")
  rt <- open_fmalloc(ops_file, mode = "scratch", size_gb = 0.1)
  on.exit({
    cleanup_fmalloc(rt)
    unlink(ops_file)
  }, add = TRUE)

  x <- create_fmalloc_vector("integer", 5, runtime = rt)
  y <- create_fmalloc_vector("integer", 5, runtime = rt)
  x[] <- c(10L, 20L, 30L, NA_integer_, 50L)
  y[] <- 1:5

  list(
    add = x + y,
    sub = x - y,
    mul = x * y,
    div = x / y,
    idiv = x %/% y,
    mod = x %% y,
    pow = x ^ 2L,
    cmp_eq = x == y,
    cmp_lt = x < 20L,
    unary_neg = -x,
    unary_not = !(x > 20L)
  )
})
#> $add
#> [1] 11 22 33 NA 55
#> attr(,"class")
#> [1] "fmalloc_vector" "fmalloc"        "integer"       
#> 
#> $sub
#> [1]  9 18 27 NA 45
#> attr(,"class")
#> [1] "fmalloc_vector" "fmalloc"        "integer"       
#> 
#> $mul
#> [1]  10  40  90  NA 250
#> attr(,"class")
#> [1] "fmalloc_vector" "fmalloc"        "integer"       
#> 
#> $div
#> [1] 10 10 10 NA 10
#> attr(,"class")
#> [1] "fmalloc_vector" "fmalloc"        "numeric"       
#> 
#> $idiv
#> [1] 10 10 10 NA 10
#> attr(,"class")
#> [1] "fmalloc_vector" "fmalloc"        "integer"       
#> 
#> $mod
#> [1]  0  0  0 NA  0
#> attr(,"class")
#> [1] "fmalloc_vector" "fmalloc"        "integer"       
#> 
#> $pow
#> [1]  100  400  900   NA 2500
#> attr(,"class")
#> [1] "fmalloc_vector" "fmalloc"        "numeric"       
#> 
#> $cmp_eq
#> [1] FALSE FALSE FALSE    NA FALSE
#> attr(,"class")
#> [1] "fmalloc_vector" "fmalloc"        "logical"       
#> 
#> $cmp_lt
#> [1]  TRUE FALSE FALSE    NA FALSE
#> attr(,"class")
#> [1] "fmalloc_vector" "fmalloc"        "logical"       
#> 
#> $unary_neg
#> [1] -10 -20 -30  NA -50
#> attr(,"class")
#> [1] "fmalloc_vector" "fmalloc"        "integer"       
#> 
#> $unary_not
#> [1]  TRUE  TRUE FALSE    NA FALSE
#> attr(,"class")
#> [1] "fmalloc_vector" "fmalloc"        "logical"
```

Mixed fmalloc + base vector and scalar recycling work:

``` r

local({
  mix_file <- tempfile(fileext = ".bin")
  rt <- open_fmalloc(mix_file, mode = "scratch", size_gb = 0.1)
  on.exit({
    cleanup_fmalloc(rt)
    unlink(mix_file)
  }, add = TRUE)

  x <- create_fmalloc_vector("numeric", 4, runtime = rt)
  x[] <- c(1.5, 2.5, 3.5, 4.5)

  list(
    fm_plus_scalar = x + 10,
    scalar_plus_fm = 10 + x,
    fm_plus_base   = x + c(100, 200, 300, 400),
    base_plus_fm   = c(1, 2, 3, 4) + x,
    recycling_ok   = x + c(10, 20),
    logical_op     = (x > 2) & (x < 5)
  )
})
#> $fm_plus_scalar
#> [1] 11.5 12.5 13.5 14.5
#> attr(,"class")
#> [1] "fmalloc_vector" "fmalloc"        "numeric"       
#> 
#> $scalar_plus_fm
#> [1] 11.5 12.5 13.5 14.5
#> attr(,"class")
#> [1] "fmalloc_vector" "fmalloc"        "numeric"       
#> 
#> $fm_plus_base
#> [1] 101.5 202.5 303.5 404.5
#> attr(,"class")
#> [1] "fmalloc_vector" "fmalloc"        "numeric"       
#> 
#> $base_plus_fm
#> [1] 2.5 4.5 6.5 8.5
#> attr(,"class")
#> [1] "fmalloc_vector" "fmalloc"        "numeric"       
#> 
#> $recycling_ok
#> [1] 11.5 22.5 13.5 24.5
#> attr(,"class")
#> [1] "fmalloc_vector" "fmalloc"        "numeric"       
#> 
#> $logical_op
#> [1] FALSE  TRUE  TRUE  TRUE
#> attr(,"class")
#> [1] "fmalloc_vector" "fmalloc"        "logical"
```

## Matrix products

`%*%`, [`crossprod()`](https://rdrr.io/r/base/crossprod.html), and
[`tcrossprod()`](https://rdrr.io/r/base/crossprod.html) on fmalloc
matrices call BLAS `dgemm` when both operands are finite doubles, and
managed native loops otherwise (the same split base R’s default matrix
product uses). Results are fmalloc-backed.

``` r

local({
  mm_file <- tempfile(fileext = ".bin")
  rt <- open_fmalloc(mm_file, mode = "scratch", size_gb = 0.1)
  on.exit({
    cleanup_fmalloc(rt)
    unlink(mm_file)
  }, add = TRUE)

  a <- create_fmalloc_matrix("numeric", nrow = 200, ncol = 80, runtime = rt)
  b <- create_fmalloc_matrix("numeric", nrow = 80, ncol = 30, runtime = rt)
  set.seed(1)
  a[] <- rnorm(200 * 80)
  b[] <- rnorm(80 * 30)

  z <- a %*% b
  list(
    dims = dim(z),
    fmalloc_backed = is_fmalloc_vector(z),
    matches_base = all.equal(as.vector(z), as.vector(matrix(a[], 200) %*% matrix(b[], 80)))
  )
})
#> $dims
#> [1] 200  30
#> 
#> $fmalloc_backed
#> [1] TRUE
#> 
#> $matches_base
#> [1] TRUE
```

## Typed tensors and ALP compression

A typed tensor is an fmalloc raw payload in a foreign element encoding
plus dtype/dims tags. Matrix products decode the payload in bounded,
block-aligned column panels streamed through `dgemm`, so the full double
representation is never materialized. Codecs `f64`, `f32`, `f16`, and
`bf16` are builtin; downstream packages register more through the C API
(Rgguf adds the quantized GGUF formats).

The builtin `"alp"` codec compresses decimal-scaled doubles losslessly
(Afroozeh et al., ALP; scalar core adapted from the MIT-licensed
[zap](https://github.com/coolbutuseless/zap) implementation, see
`inst/COPYRIGHTS`):

``` r

local({
  alp_file <- tempfile(fileext = ".bin")
  rt <- open_fmalloc(alp_file, mode = "scratch", size_gb = 0.5)
  on.exit({
    cleanup_fmalloc(rt)
    unlink(alp_file)
  }, add = TRUE)

  set.seed(2)
  # Dosage-like decimal data: 3 decimals in [0, 2].
  x <- matrix(round(runif(20000 * 10, 0, 2), 3), nrow = 20000)
  ten <- as_fmalloc_tensor(x, runtime = rt)
  print(ten)

  b <- matrix(rnorm(10 * 4), nrow = 10)
  z <- ten %*% b # decoded panel-by-panel inside dgemm

  list(
    codecs = fmalloc_tensor_codecs(),
    compression_ratio = length(unclass(ten)) / (length(x) * 8),
    lossless = identical(as.vector(fmalloc_tensor_materialize(ten)[]), as.vector(x)),
    product_fmalloc_backed = is_fmalloc_vector(z),
    product_matches_base = all.equal(as.vector(z), as.vector(x %*% b))
  )
})
#> <fmalloc_tensor alp [20000 x 10], 279720 payload bytes>
#> $codecs
#> [1] "f64"    "f32"    "f16"    "bf16"   "alp"    "sparse"
#> 
#> $compression_ratio
#> [1] 0.174825
#> 
#> $lossless
#> [1] TRUE
#> 
#> $product_fmalloc_backed
#> [1] TRUE
#> 
#> $product_matches_base
#> [1] TRUE
```

The `"sparse"` codec stores only the nonzeros of each chunk, for
mostly-zero data such as single-cell counts — it compresses heavily and
still multiplies through the panel engine:

``` r

local({
  sp_file <- tempfile(fileext = ".bin")
  rt <- open_fmalloc(sp_file, mode = "scratch", size_gb = 0.5)
  on.exit({ cleanup_fmalloc(rt); unlink(sp_file) }, add = TRUE)

  set.seed(3)
  # ~10% nonzero small counts (single-cell-like)
  X <- matrix(0, 5000, 40)
  nz <- sample(length(X), length(X) * 0.1)
  X[nz] <- rpois(length(nz), 3) + 1

  ten <- as_fmalloc_tensor(X, dtype = "sparse", runtime = rt)
  b <- matrix(rnorm(40 * 3), nrow = 40)

  list(
    dtype = fmalloc_tensor_dtype(ten),
    compression_ratio = length(unclass(ten)) / (length(X) * 8),
    lossless = identical(as.vector(fmalloc_tensor_materialize(ten)[]), as.vector(X)),
    product_matches_base = all.equal(as.vector((ten %*% b)[]), as.vector(X %*% b))
  )
})
#> $dtype
#> [1] "sparse"
#> 
#> $compression_ratio
#> [1] 0.1522
#> 
#> $lossless
#> [1] TRUE
#> 
#> $product_matches_base
#> [1] TRUE
```

## Out-of-core matrix products

[`fmalloc_matmul_ooc()`](https://sounkou-bioinfo.github.io/Rfmalloc/reference/fmalloc_matmul_ooc.md),
[`fmalloc_crossprod_ooc()`](https://sounkou-bioinfo.github.io/Rfmalloc/reference/fmalloc_matmul_ooc.md),
and
[`fmalloc_tcrossprod_ooc()`](https://sounkou-bioinfo.github.io/Rfmalloc/reference/fmalloc_matmul_ooc.md)
stream a matrix through `dgemm` one contiguous column tile at a time,
releasing each tile’s pages with `madvise(MADV_DONTNEED)`, so a matrix
larger than RAM multiplies with a resident set bounded by the tile size
rather than the matrix size. `crossprod(X)` = `X'X` is the
covariance/Gram matrix behind PCA, ridge, and GWAS.

`%*%`/[`crossprod()`](https://rdrr.io/r/base/crossprod.html)/[`tcrossprod()`](https://rdrr.io/r/base/crossprod.html)
on an fmalloc matrix auto-route to these when the operand reaches
`getOption("Rfmalloc.ooc_threshold_gb")` (default half of physical RAM).
The example forces the threshold to 0 to show the path on a small
matrix:

``` r

local({
  ooc_file <- tempfile(fileext = ".bin")
  rt <- open_fmalloc(ooc_file, mode = "scratch", size_gb = 0.5)
  on.exit({
    cleanup_fmalloc(rt); unlink(ooc_file)
    options(Rfmalloc.ooc_threshold_gb = NULL, Rfmalloc.ooc_tile_mb = NULL)
  }, add = TRUE)

  set.seed(4)
  X <- create_fmalloc_matrix("numeric", nrow = 4000, ncol = 200, runtime = rt)
  bx <- matrix(rnorm(4000 * 200), 4000, 200)
  X[] <- bx

  # explicit out-of-core Gram matrix
  G <- fmalloc_crossprod_ooc(X, tile_mb = 8)

  # auto-routing: force the threshold so crossprod(X) takes the OOC path
  options(Rfmalloc.ooc_threshold_gb = 0, Rfmalloc.ooc_tile_mb = 8)
  list(
    gram_matches_base = all.equal(as.vector(G[]), as.vector(crossprod(bx))),
    gram_is_fmalloc_backed = is_fmalloc_vector(G),
    auto_routed_matches = all.equal(as.vector(crossprod(X)[]), as.vector(crossprod(bx)))
  )
})
#> $gram_matches_base
#> [1] TRUE
#> 
#> $gram_is_fmalloc_backed
#> [1] TRUE
#> 
#> $auto_routed_matches
#> [1] TRUE
```

## In-place mutation

On an *unshared* fmalloc vector, ordinary `x[i] <- value` already writes
in place through the ALTREP data pointer (no copy — a benefit of the
file-backed ALTREP design). The copy that hurts happens when the vector
is *shared* (`y <- x`): R then duplicates the whole payload to preserve
value semantics, which is catastrophic when it is larger than RAM.
[`fmalloc_set()`](https://sounkou-bioinfo.github.io/Rfmalloc/reference/fmalloc_insitu.md)/
[`fmalloc_fill()`](https://sounkou-bioinfo.github.io/Rfmalloc/reference/fmalloc_insitu.md)
and
[`fmalloc_add()`](https://sounkou-bioinfo.github.io/Rfmalloc/reference/fmalloc_insitu.md)/[`sub()`](https://rdrr.io/r/base/grep.html)/`mul()`/`div()`
mutate by reference *regardless of sharing*, so all bindings observe the
change — deliberately, and only ever through these explicit functions,
never a silent `[<-`.

``` r

local({
  ip_file <- tempfile(fileext = ".bin")
  rt <- open_fmalloc(ip_file, mode = "scratch", size_gb = 0.1)
  on.exit({ cleanup_fmalloc(rt); unlink(ip_file) }, add = TRUE)

  x <- create_fmalloc_vector("numeric", 6, runtime = rt)
  fmalloc_fill(x, 1)          # x[] <- 1, no copy
  fmalloc_set(x, c(2, 4), 9)  # x[c(2,4)] <- 9, no copy
  fmalloc_add(x, 10)          # x <- x + 10, in place

  y <- x                      # no copy on assignment
  fmalloc_mul(x, 2)           # mutating x also changes its alias y

  list(x = x[], alias_y = y[])
})
#> $x
#> [1] 22 38 22 38 22 22
#> 
#> $alias_y
#> [1] 22 38 22 38 22 22
```

## Pluggable matmul backends

The matrix-product kernels dispatch their `dgemm` through a selectable
backend. The default is R’s BLAS (itself user-swappable via
`update-alternatives`, `LD_PRELOAD`, or FlexiBLAS). Downstream packages
register an alternative (e.g. a GPU or out-of-core-aware GEMM) through
the `Rfmalloc_register_matmul_backend` C-callable; selection is
Rfmalloc-scoped, so base R’s `%*%` is unaffected.

``` r

fmalloc_matmul_backend()   # "blas" by default
#> [1] "blas"
fmalloc_matmul_backends()  # names registered by other packages (none here)
#> character(0)
```

## Explicit destruction and parent safety

[`destroy_fmalloc_vector()`](https://sounkou-bioinfo.github.io/Rfmalloc/reference/destroy_fmalloc_vector.md)
immediately releases native bookkeeping for one fmalloc ALTREP vector
and returns a logical indicating whether a live vector was destroyed.
The helper enforces parent-reference safety: a vector cannot be
destroyed while it is still stored as a child of any fmalloc list. When
needed, drop parent links first.

``` r


local({
  destroy_file <- tempfile(fileext = ".bin")
  rt <- open_fmalloc(destroy_file, mode = "persistent")
  on.exit({
    cleanup_fmalloc(rt)
    unlink(destroy_file)
  }, add = TRUE)

  child <- create_fmalloc_vector("integer", 2, runtime = rt)
  child[] <- 1:2

  parent <- create_fmalloc_vector("list", 1, runtime = rt)
  parent[[1]] <- child

  destroy_error <- try(destroy_fmalloc_vector(child), silent = TRUE)

  parent[[1]] <- NULL
  destroy_ok <- destroy_fmalloc_vector(child)

  list(
    destroy_rejected_with_parent = inherits(destroy_error, "try-error"),
    destroy_succeeded_after_unset = destroy_ok
  )
})
#> $destroy_rejected_with_parent
#> [1] TRUE
#> 
#> $destroy_succeeded_after_unset
#> [1] TRUE
```

In persistent mode, destroying with default semantics retains payload
bytes so the recorded on-disk allocation remains recoverable by normal
[`serialize()`](https://rdrr.io/r/base/serialize.html) flows. Use
`destroy_fmalloc_vector(x, unsafe = TRUE)` to reclaim persistent payload
memory and mark the catalog entry as non-recoverable.

This is scoped to the targeted vector(s): if one object is
unsafe-destroyed, other objects in the same runtime remain recoverable.

``` r


local({
  selective_file <- tempfile(fileext = ".bin")
  rt <- open_fmalloc(selective_file, mode = "persistent")
  on.exit({
    cleanup_fmalloc(rt)
    unlink(selective_file)
  }, add = TRUE)

  keep <- create_fmalloc_vector("integer", 3, runtime = rt)
  drop <- create_fmalloc_vector("character", 2, runtime = rt)
  keep[] <- c(101L, 202L, 303L)
  drop[] <- c("alpha", "beta")

  keep_blob <- serialize(keep, NULL)
  drop_blob <- serialize(drop, NULL)

  destroy_fmalloc_vector(drop, unsafe = TRUE)

  keep_recovered <- unserialize(keep_blob)
  drop_recover_error <- try(unserialize(drop_blob), silent = TRUE)

  list(
    keep_recovered_ok = all.equal(keep_recovered[], c(101L, 202L, 303L)) == TRUE,
    drop_recover_fails = inherits(drop_recover_error, "try-error")
  )
})
#> $keep_recovered_ok
#> [1] TRUE
#> 
#> $drop_recover_fails
#> [1] TRUE
```

## Runtime Modes

[`open_fmalloc()`](https://sounkou-bioinfo.github.io/Rfmalloc/reference/open_fmalloc.md)
and
[`init_fmalloc()`](https://sounkou-bioinfo.github.io/Rfmalloc/reference/init_fmalloc.md)
accept `mode = "persistent"` or `mode = "scratch"`. The default is
`"persistent"`.

- In persistent mode, committed vector payloads are not returned to
  fmalloc by vector finalizers. Fixed-width atomic vectors serialize by
  reference to the physical allocation in the backing file.
- In scratch mode, the backing file is a temporary allocation arena.
  Vector finalizers may return payloads to fmalloc, and serialization
  falls back to ordinary R values.

``` r


local({
  persist_file <- tempfile(fileext = ".bin")
  rt <- open_fmalloc(persist_file, mode = "persistent")
  on.exit({
    cleanup_fmalloc(rt)
    unlink(persist_file)
  }, add = TRUE)

  x <- create_fmalloc_vector("integer", 5, runtime = rt)
  x[] <- 1:5
  roundtrip <- unserialize(serialize(x, NULL))
  roundtrip[]
})
#> [1] 1 2 3 4 5
```

## Inspecting fmalloc ALTREP Metadata

R’s internal inspector dispatches to the ALTREP `Inspect` hook.
Rfmalloc’s inspector reports the vector type, length, payload byte
count, runtime mode, runtime state, payload offset, file UUID, and
backing-file path.

``` r


local({
  inspect_file <- tempfile(fileext = ".bin")
  rt <- open_fmalloc(inspect_file, mode = "persistent")
  on.exit({
    cleanup_fmalloc(rt)
    unlink(inspect_file)
  }, add = TRUE)

  inspect_vec <- create_fmalloc_vector("integer", 4, runtime = rt)
  inspect_vec[] <- 1:4
  .Internal(inspect(inspect_vec))
})
#> @5c52e2e2f568 13 INTSXP g0c0 [OBJ,REF(1),ATT] fmalloc_altrep type=integer length=4 bytes=16 data=0x7155038023e8 mode=persistent runtime=open offset=9192 uuid=09adecd5d33e6e7a64e288c053d9d927 file=/tmp/RtmptHuDPO/file14ea3bba7fdeb.bin
#> ATTRIB:
#>   @5c52e2e2e5a8 02 LISTSXP g0c0 [REF(1)] 
#>     TAG: @5c52de887e40 01 SYMSXP g1c0 [MARK,REF(38394),LCK,gp=0x6000] "class" (has value)
#>     @5c52e39eefe8 16 STRSXP g0c3 [REF(65535)] (len=3, tl=0)
#>       @5c52e3dabb38 09 CHARSXP g0c2 [MARK,REF(58),gp=0x60] [ASCII] [cached] "fmalloc_vector"
#>       @5c52e375dbc0 09 CHARSXP g0c1 [MARK,REF(210),gp=0x60] [ASCII] [cached] "fmalloc"
#>       @5c52de8badf8 09 CHARSXP g1c1 [MARK,REF(623),gp=0x61] [ASCII] [cached] "integer"
```

`inspect()` output is an internal R diagnostic, so exact formatting can
vary between R versions. The `fmalloc_altrep ...` line comes from
Rfmalloc’s ALTREP `Inspect` method.

## Character Vectors

Rfmalloc character vectors are ALTSTRING vectors. String bytes, lengths,
encodings, and NA flags live in fmalloc storage. R `CHARSXP` values are
materialized on `STRING_ELT()` access; Rfmalloc does not allocate
`CHARSXP` objects inside fmalloc.

``` r


local({
  char_file <- tempfile(fileext = ".bin")
  rt <- open_fmalloc(char_file, mode = "persistent")
  on.exit({
    cleanup_fmalloc(rt)
    unlink(char_file)
  }, add = TRUE)

  chars <- create_fmalloc_vector("character", 3, runtime = rt)
  chars[] <- c("one", NA_character_, "three")

  from_int <- create_fmalloc_vector("integer", 3, runtime = rt)
  from_int[] <- 1:3

  list(
    chars = chars[],
    from_integer = as.character(from_int)[]
  )
})
#> $chars
#> [1] "one"   NA      "three"
#> 
#> $from_integer
#> [1] "1" "2" "3"
```

## Matrix and data.frame constructors and converters

Use constructor helpers for shape-aware allocation, and `as_fmalloc_*()`
helpers to install shape metadata on existing fmalloc-backed vectors:

``` r


local({
  ctor_file <- tempfile(fileext = ".bin")
  rt <- open_fmalloc(ctor_file, mode = "persistent")
  on.exit({
    cleanup_fmalloc(rt)
    unlink(ctor_file)
  }, add = TRUE)

  m <- create_fmalloc_matrix("integer", nrow = 2L, ncol = 3L, runtime = rt)
  m[] <- 1:6

  base_vec <- create_fmalloc_vector("integer", 6L, runtime = rt)
  base_vec[] <- 11:16
  m_view <- as_fmalloc_matrix(base_vec, ncol = 3L, copy = FALSE)

  # Metadata-only reshape, then mutate through the reshaped object
  m_view[1L, 2L] <- 88L

  a <- create_fmalloc_array("numeric", dim = c(2L, 1L, 3L), runtime = rt)
  a[] <- 1:6

  col_a <- create_fmalloc_vector("integer", 3L, runtime = rt)
  col_b <- create_fmalloc_vector("integer", 3L, runtime = rt)
  col_a[] <- c(1L, 2L, 3L)
  col_b[] <- c(4L, 5L, 6L)
  df <- as_fmalloc_data_frame(a = col_a, b = col_b, stringsAsFactors = FALSE)

  list(
    matrix_dims = dim(m),
    metadata_shares_payload = c(base_vec[3L], m_view[1L, 2L]),
    array_dims = dim(a),
    df_columns = names(df)
  )
})
#> $matrix_dims
#> [1] 2 3
#> 
#> $metadata_shares_payload
#> [1] 13 88
#> 
#> $array_dims
#> [1] 2 1 3
#> 
#> $df_columns
#> [1] "a" "b"
```

## Reduction output materialization

[`rowSums()`](https://sounkou-bioinfo.github.io/Rfmalloc/reference/fmalloc_reduction_methods.md),
[`colSums()`](https://sounkou-bioinfo.github.io/Rfmalloc/reference/fmalloc_reduction_methods.md),
[`rowMeans()`](https://sounkou-bioinfo.github.io/Rfmalloc/reference/fmalloc_reduction_methods.md),
[`colMeans()`](https://sounkou-bioinfo.github.io/Rfmalloc/reference/fmalloc_reduction_methods.md),
and other `Summary` pathways keep small results in regular R objects by
default to avoid unnecessary small temporary mappings. You can tune this
behavior with:

- `options(Rfmalloc.reduce_result_length = n)`

where `n` is the maximum allowed result length to keep in-memory.
Results with length greater than `n` stay fmalloc-backed.

``` r


local({
  reduce_file <- tempfile(fileext = ".bin")
  rt <- open_fmalloc(reduce_file, mode = "persistent")
  old_limit <- getOption("Rfmalloc.reduce_result_length")
  on.exit({
    cleanup_fmalloc(rt)
    options(Rfmalloc.reduce_result_length = old_limit)
    unlink(reduce_file)
  }, add = TRUE)

  m <- create_fmalloc_matrix("integer", nrow = 4L, ncol = 4L, runtime = rt)
  m[] <- 1:16

  base_m <- matrix(1:16, nrow = 4L, ncol = 4L)
  tiny_vec <- create_fmalloc_vector("integer", 4L, runtime = rt)
  tiny_vec[] <- 11:14

  options(Rfmalloc.reduce_result_length = 5L)
  default_row_sums <- rowSums(m)
  default_range <- range(tiny_vec)

  options(Rfmalloc.reduce_result_length = 3L)
  compact_row_sums <- rowSums(m)

  options(Rfmalloc.reduce_result_length = 1L)
  compact_range <- range(tiny_vec)

  list(
    default_row_sums_in_memory = inherits(default_row_sums, "fmalloc"),
    compact_row_sums_filebacked = inherits(compact_row_sums, "fmalloc"),
    default_range_in_memory = inherits(default_range, "fmalloc"),
    compact_range_filebacked = inherits(compact_range, "fmalloc"),
    rowSums_values = unclass(compact_row_sums),
    expected_rowSums = rowSums(base_m)
  )
})
#> $default_row_sums_in_memory
#> [1] FALSE
#> 
#> $compact_row_sums_filebacked
#> [1] TRUE
#> 
#> $default_range_in_memory
#> [1] FALSE
#> 
#> $compact_range_filebacked
#> [1] TRUE
#> 
#> $rowSums_values
#> [1] 28 32 36 40
#> 
#> $expected_rowSums
#> [1] 28 32 36 40
```

## Multiple Runtimes and Lifetime

Runtime handles make it possible to use more than one backing file in
one R process.

``` r


local({
  file_a <- tempfile(fileext = ".bin")
  file_b <- tempfile(fileext = ".bin")

  rt_a <- open_fmalloc(file_a)
  rt_b <- open_fmalloc(file_b, size_gb = 0.1)

  vec_a <- create_fmalloc_vector("integer", 10, runtime = rt_a)
  vec_b <- create_fmalloc_vector("numeric", 10, runtime = rt_b)
  vec_a[1] <- 101L
  vec_b[1] <- 202

  before_cleanup <- data.frame(
    vector = c("vec_a", "vec_b"),
    value = c(vec_a[1], vec_b[1])
  )

  cleanup_fmalloc(rt_a)
  after_cleanup <- vec_a[1]
  cleanup_fmalloc(rt_b)
  unlink(c(file_a, file_b))

  list(
    before_cleanup = before_cleanup,
    vec_a_after_cleanup = after_cleanup
  )
})
#> $before_cleanup
#>   vector value
#> 1  vec_a   101
#> 2  vec_b   202
#> 
#> $vec_a_after_cleanup
#> [1] 101
```

Calling `cleanup_fmalloc(rt_a)` marks `rt_a` closed. If vectors from
`rt_a` are still reachable, the native mapping is kept alive until those
vectors are garbage-collected. This is implemented with a native
live-vector count and an ALTREP-held external pointer for each
fmalloc-backed vector; no user-visible attribute is added to the vector.

## Serialization and Persistent References

Rfmalloc has an ALTREP serialization path for persistent runtimes. For
persistent fixed-width atomic and character vectors,
[`serialize()`](https://rdrr.io/r/base/serialize.html) stores a small
reference record instead of copying the vector payload into the
serialization stream. The record contains:

- format tag: `"RfmallocRef"`;
- reference format version;
- backing-file path;
- backing-file UUID;
- R vector type;
- vector length;
- payload offset inside the mapped file;
- payload byte size.

During [`unserialize()`](https://rdrr.io/r/base/serialize.html),
Rfmalloc reopens the backing file, verifies the UUID, checks the
recorded dimensions and file bounds, validates the catalog record and
generation, and reconstructs an ALTREP vector around the same fmalloc
allocation.

``` r


local({
  ser_file <- tempfile(fileext = ".bin")
  rt <- open_fmalloc(ser_file, mode = "persistent")

  ints <- create_fmalloc_vector("integer", 4, runtime = rt)
  ints[] <- 101:104
  chars <- create_fmalloc_vector("character", 3, runtime = rt)
  chars[] <- c("alpha", NA_character_, "gamma")

  catalog <- list_fmalloc_allocations(rt)
  ints_blob <- serialize(ints, NULL)
  chars_blob <- serialize(chars, NULL)

  cleanup_fmalloc(rt)

  ints_recovered <- unserialize(ints_blob)
  chars_recovered <- unserialize(chars_blob)
  output <- list(
    catalog = catalog[, c("record_offset", "generation", "type", "length")],
    integers = ints_recovered[],
    characters = chars_recovered[]
  )

  unlink(ser_file)
  output
})
#> $catalog
#>   record_offset generation      type length
#> 1          9440          2 character      3
#> 2          9224          1   integer      4
#> 
#> $integers
#> [1] 101 102 103 104
#> 
#> $characters
#> [1] "alpha" NA      "gamma"
```

## Catalog diagnostics

Use
[`diagnose_fmalloc_runtime()`](https://sounkou-bioinfo.github.io/Rfmalloc/reference/diagnose_fmalloc_runtime.md)
to inspect runtime metadata, the live catalog, and a compact summary
that can help decide when a runtime is a reset candidate:

``` r

local({
  diag_file <- tempfile(fileext = ".bin")
  rt <- open_fmalloc(diag_file, mode = "persistent")

  a <- create_fmalloc_vector("integer", 4, runtime = rt)
  b <- create_fmalloc_vector("character", 2, runtime = rt)
  a[] <- 1:4
  b[] <- c("left", "right")

  pre <- diagnose_fmalloc_runtime(rt)
  destroy_fmalloc_vector(a)
  post <- diagnose_fmalloc_runtime(rt)

  list(
    mode = post$runtime$mode,
    pre_records = pre$summary$record_count,
    pre_committed = pre$summary$committed_records,
    post_records = post$summary$record_count,
    post_tombstones = post$summary$tombstoned_records,
    compaction_implemented = post$summary$compaction_implemented
  )
})
#> $mode
#> [1] "persistent"
#> 
#> $pre_records
#> [1] 2
#> 
#> $pre_committed
#> [1] 2
#> 
#> $post_records
#> [1] 2
#> 
#> $post_tombstones
#> [1] 1
#> 
#> $compaction_implemented
#> [1] FALSE
```

Scratch runtimes use ordinary R serialization instead. Their serialized
values do not depend on reopening the scratch backing file.

``` r

local({
  scratch_file <- tempfile(fileext = ".bin")
  scratch_rt <- open_fmalloc(scratch_file, mode = "scratch")

  scratch_vec <- create_fmalloc_vector("integer", 4, runtime = scratch_rt)
  scratch_vec[] <- 1:4
  scratch_copy <- unserialize(serialize(scratch_vec, NULL))

  cleanup_fmalloc(scratch_rt)
  unlink(scratch_file)
  scratch_copy
})
#> [1] 1 2 3 4
#> attr(,"class")
#> [1] "fmalloc_vector" "fmalloc"        "integer"
```

Rfmalloc list vectors are intentionally not general R lists. Element
assignment accepts only `NULL` or Rfmalloc-backed vectors from the same
runtime; ordinary R objects such as base vectors, data frames, or
arbitrary lists are rejected. This keeps lists inside the same
file-backed object universe.

For persistent runtimes, list containers now serialize as reference
state when all children are recoverable fmalloc vectors. The serialized
state stores per-slot child descriptors, so nested list containers can
reopen recursively from the same backing file without carrying
session-local `SEXP` pointers.

This is reference-based recovery, not name-based object discovery. The
catalog stores physical allocation records, not user variable names.
Serialized references use the catalog record offset and generation for
validation; the catalog can be listed, but it is not yet a high-level
object store for recovering vectors by name.

## List constraints and recovery examples

List assignment rejects non-fmalloc payloads at runtime.

``` r


local({
  reject_file <- tempfile(fileext = ".bin")
  rt <- open_fmalloc(reject_file, mode = "persistent")
  on.exit({
    cleanup_fmalloc(rt)
    unlink(reject_file)
  }, add = TRUE)

  fm_list <- create_fmalloc_vector("list", 2, runtime = rt)
  child <- create_fmalloc_vector("integer", 2, runtime = rt)
  child[] <- c(1L, 2L)
  fm_list[[1]] <- child

  bad_assignment <- try({
    fm_list[[2]] <- 1:2
  }, silent = TRUE)

  list(
    same_runtime_child = fm_list[[1]][],
    rejected_non_fmalloc_payload = inherits(bad_assignment, "try-error")
  )
})
#> $same_runtime_child
#> [1] 1 2
#> 
#> $rejected_non_fmalloc_payload
#> [1] TRUE
```

Nested fmalloc lists recover recursively when serialized and
unserialized from a persistent backing file.

``` r


local({
  recover_file <- tempfile(fileext = ".bin")
  rt <- open_fmalloc(recover_file, mode = "persistent")
  on.exit(unlink(recover_file), add = TRUE)

  child <- create_fmalloc_vector("integer", 2, runtime = rt)
  child[] <- 1:2

  nested <- create_fmalloc_vector("list", 1, runtime = rt)
  nested[[1]] <- child

  labels <- create_fmalloc_vector("character", 2, runtime = rt)
  labels[] <- c("alpha", "beta")

  fm_list <- create_fmalloc_vector("list", 2, runtime = rt)
  fm_list[[1]] <- nested
  fm_list[[2]] <- labels

  blob <- serialize(fm_list, NULL)

  cleanup_fmalloc(rt)

  recovered <- unserialize(blob)
  list(
    recovered_nested = recovered[[1]][[1]][],
    recovered_labels = recovered[[2]]
  )
})
#> $recovered_nested
#> [1] 1 2
#> 
#> $recovered_labels
#> [1] "alpha" "beta"
```

Cross-runtime insertion is also rejected.

``` r


local({
  cross_file_a <- tempfile(fileext = ".bin")
  cross_file_b <- tempfile(fileext = ".bin")

  rt_a <- open_fmalloc(cross_file_a, mode = "persistent")
  rt_b <- open_fmalloc(cross_file_b, mode = "persistent")
  on.exit({
    cleanup_fmalloc(rt_a)
    cleanup_fmalloc(rt_b)
    unlink(c(cross_file_a, cross_file_b))
  }, add = TRUE)

  child_other_runtime <- create_fmalloc_vector("integer", 2, runtime = rt_b)
  child_other_runtime[] <- c(10L, 20L)

  fm_list <- create_fmalloc_vector("list", 1, runtime = rt_a)
  cross_runtime_error <- try({
    fm_list[[1]] <- child_other_runtime
  }, silent = TRUE)

  list(cross_runtime_rejected = inherits(cross_runtime_error, "try-error"))
})
#> $cross_runtime_rejected
#> [1] TRUE
```

## Performance

Rfmalloc vectors are regular R vectors from R’s point of view, but
different operations exercise different paths through R and ALTREP:

- vectorized sequential operations such as `sum(x)` can use contiguous
  payload access;
- scalar loops use ALTREP element access and are more call-heavy;
- `x[i]` creates a fmalloc-backed subset copy for supported vector
  types;
- [`bench::mark()`](https://bench.r-lib.org/reference/mark.html) reports
  R heap allocation in `mem_alloc`; it does not count bytes stored in
  the fmalloc mapped file.

The small benchmark below uses `bench` and a scratch runtime, so
benchmark-only fmalloc allocations are not preserved as persistent
records. Timings are machine- and R-version-specific; use this as a
template for local measurements.

``` r


perf_file <- tempfile(fileext = ".bin")
rt <- open_fmalloc(perf_file, mode = "scratch", size_gb = 0.1)

n <- 100000L
set.seed(1)
idx <- sample.int(n, 2000L)

base_int <- integer(n)
base_int[] <- seq_len(n)

fm_int <- create_fmalloc_vector("integer", n, runtime = rt)
fm_int[] <- base_int

write_env <- new.env(parent = emptyenv())
write_env$base <- base_int
write_env$fm <- create_fmalloc_vector("integer", n, runtime = rt)
write_env$fm[] <- base_int

scalar_sum <- function(x, i) {
  total <- 0L
  for (j in i) total <- total + x[[j]]
  total
}

perf_result <- bench::mark(
  base_sequential_sum = sum(base_int),
  fmalloc_sequential_sum = sum(fm_int),
  base_scalar_read = scalar_sum(base_int, idx),
  fmalloc_scalar_read = scalar_sum(fm_int, idx),
  base_subset_copy = base_int[idx],
  fmalloc_subset_copy = fm_int[idx],
  base_indexed_write = {
    write_env$base[idx] <- 0L
    invisible(write_env$base[1L])
  },
  fmalloc_indexed_write = {
    write_env$fm[idx] <- 0L
    invisible(write_env$fm[1L])
  },
  iterations = 20,
  check = FALSE
)[, c("expression", "median", "itr/sec", "mem_alloc", "gc/sec")]

rm(fm_int, write_env)
invisible(gc())
cleanup_fmalloc(rt)
unlink(perf_file)
perf_result
#> # A tibble: 8 × 5
#>   expression               median `itr/sec` mem_alloc `gc/sec`
#>   <bch:expr>             <bch:tm>     <dbl> <bch:byt>    <dbl>
#> 1 base_sequential_sum     36.84µs    25742.        0B        0
#> 2 fmalloc_sequential_sum  42.38µs    22750.        0B        0
#> 3 base_scalar_read        33.55µs    28674.        0B        0
#> 4 fmalloc_scalar_read    896.96µs     1114.   24.55KB        0
#> 5 base_subset_copy         3.52µs   255065.    7.86KB        0
#> 6 fmalloc_subset_copy     19.68µs    45277.        0B        0
#> 7 base_indexed_write      27.99µs    34122.  390.67KB        0
#> 8 fmalloc_indexed_write  191.25µs     5095.        0B        0
```

## Native C API for Other Packages

Rfmalloc installs `inst/include/Rfmalloc.h` and registers C-callable
entry points with `R_RegisterCCallable()`. Downstream packages can add
Rfmalloc to `LinkingTo` and `Imports`, include the header, and use the
inline wrappers.

The current native surface (API version 5) exposes runtime open/cleanup,
vector creation, default-runtime lookup and synchronization, catalog
listing, runtime/vector introspection, vector destruction, an
API-version query, and the two extension registries:
`Rfmalloc_register_tensor_codec` (plug an element encoding into the
panel-streamed matrix products —
[Rgguf](https://github.com/sounkou-bioinfo/Rgguf) uses this for the
quantized GGUF formats) and `Rfmalloc_register_matmul_backend` (plug a
GEMM kernel behind the matrix products). Returned `SEXP` objects follow
normal R API ownership rules.

## References

Storage / allocator:

- fmalloc: <https://github.com/yasukata/fmalloc>
- Simon Urbanek’s `Rf_allocVector3()` custom mmap allocator PoC:
  <https://gist.github.com/s-u/6712c97ca74181f5a1a5>
- Rfmalloc’s prior custom allocator implementation using
  `Rf_allocVector3()` (for historical comparison):
  <https://github.com/sounkou-bioinfo/Rfmalloc/commit/0165953>

Compression:

- ALP (Adaptive Lossless floating-Point Compression), Afroozeh, Kuffo &
  Boncz, <https://doi.org/10.1145/3626717>; reference implementation
  <https://github.com/cwida/ALP>. Rfmalloc’s `"alp"` codec adapts the
  scalar core from Mike Cheng’s MIT-licensed
  [zap](https://github.com/coolbutuseless/zap).

In-place / by-reference mutation:

- insitu (modify R vectors by reference, bypassing copy-on-modify):
  <https://github.com/coolbutuseless/insitu>

Related work (memory-mapped / larger-than-RAM matrices in R):

- houba — memory-mapped vectors/matrices/arrays:
  <https://github.com/HervePerdry/houba>
- bigstatsr / bigsnpr (FBM): <https://privefl.github.io/bigstatsr/>,
  <https://privefl.github.io/bigsnpr/>
- bigmemory: <https://cran.r-project.org/package=bigmemory>
- vroom (lazy file index): <https://vroom.r-lib.org>

## License

GPL (\>= 2)
