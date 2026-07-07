# Out-of-core matrix product for fmalloc matrices larger than RAM

Computes `A %*% x` where `A` is a large column-major fmalloc-backed
double matrix living in the backing file and `x` is an ordinary numeric
vector or matrix. `A` is consumed one contiguous column tile at a time:
each tile is multiplied into the accumulator with BLAS `dgemm`, then its
pages are released with `madvise(MADV_DONTNEED)`, so the resident set
stays bounded by `tile_mb` rather than the size of `A`. This lets `A`
exceed physical RAM.

## Usage

``` r
fmalloc_matmul_ooc(A, x, tile_mb = 256)

fmalloc_crossprod_ooc(A, tile_mb = 256)
```

## Arguments

- A:

  An fmalloc-backed double matrix (`m x n`).

- x:

  A numeric vector of length `n`, or a numeric matrix (`n x k`).

- tile_mb:

  Target resident megabytes per column tile of `A`. Larger tiles
  amortize BLAS overhead; smaller tiles bound peak memory more tightly.
  Defaults to 256.

## Value

An fmalloc-backed double matrix (`m x k`), equal to `A %*% x`.

## Details

The backing storage is advised `MADV_SEQUENTIAL` so the kernel reads
ahead. The result is an fmalloc-backed matrix allocated in `A`'s
runtime.

`%*%` on an fmalloc matrix calls this automatically when the left
operand's payload reaches `getOption("Rfmalloc.ooc_threshold_gb")`
(default: half of physical RAM), using
`getOption("Rfmalloc.ooc_tile_mb", 256)` for the tile size; smaller
products keep the in-core BLAS path.
[`crossprod()`](https://rdrr.io/r/base/crossprod.html)/
[`tcrossprod()`](https://rdrr.io/r/base/crossprod.html) are not
auto-routed (their output can itself exceed RAM).

## Examples

``` r
if (FALSE) { # \dontrun{
rt <- open_fmalloc(tempfile(fileext = ".bin"), size_gb = 8)
A <- create_fmalloc_matrix("numeric", nrow = 100000, ncol = 20000, runtime = rt)
# ... fill A ...
y <- fmalloc_matmul_ooc(A, rnorm(20000), tile_mb = 128)
cleanup_fmalloc(rt)
} # }
```
