# Materialize a bit-packed haplotype store into a 0/1 matrix

Decodes an
[`fmalloc_haplotypes()`](https://sounkou-bioinfo.github.io/Rfmalloc/Rfmalloc/reference/fmalloc_haplotypes.md)
store back into an `L x N` matrix of `0L`/`1L` integer calls, variants
in rows and haplotypes in columns. The result is itself fmalloc-backed
(file-mapped storage, not an R-heap copy): this is the same "decode into
typed storage, not into a plain R object" discipline
[`fmalloc_tensor_materialize()`](https://sounkou-bioinfo.github.io/Rfmalloc/Rfmalloc/reference/fmalloc_tensor.md)
uses for the matmul codecs.

## Usage

``` r
fmalloc_hap_materialize(x, runtime = NULL)
```

## Arguments

- x:

  An `fmalloc_haplotypes` object from
  [`fmalloc_haplotypes()`](https://sounkou-bioinfo.github.io/Rfmalloc/Rfmalloc/reference/fmalloc_haplotypes.md).

- runtime:

  Runtime handle from
  [`open_fmalloc()`](https://sounkou-bioinfo.github.io/Rfmalloc/Rfmalloc/reference/open_fmalloc.md);
  defaults to the runtime established by
  [`init_fmalloc()`](https://sounkou-bioinfo.github.io/Rfmalloc/Rfmalloc/reference/init_fmalloc.md).

## Value

An fmalloc-backed integer matrix of `0L`/`1L` calls with `dim(x)`.

## Details

Passing the result straight to `kalis::CacheHaplotypes()` is the
intended use: kalis's matrix path only requires
[`is.matrix()`](https://rdrr.io/r/base/matrix.html) and
[`is.integer()`](https://rdrr.io/r/base/integer.html), both true of an
fmalloc-backed integer matrix, so no separate R-heap copy of the decoded
0/1 matrix is needed before handing it to kalis. kalis then copies the
calls once more, into its own private SIMD cache layout - that copy is
intrinsic to kalis's architecture and would happen for any input source,
fmalloc-backed or not.

## See also

[`fmalloc_haplotypes()`](https://sounkou-bioinfo.github.io/Rfmalloc/Rfmalloc/reference/fmalloc_haplotypes.md)

## Examples

``` r
rt <- open_fmalloc(tempfile(), size_gb = 0.1)
h <- matrix(c(0L, 1L, 1L, 0L, 1L, 0L), nrow = 3, ncol = 2)
hap <- fmalloc_haplotypes(h, runtime = rt)
identical(fmalloc_hap_materialize(hap, runtime = rt)[], h)
#> [1] TRUE
cleanup_fmalloc(rt)
```
