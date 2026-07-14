# Phased haplotypes as a 1-bit fmalloc store

Encodes an `L x N` matrix of phased haplotype calls (`0`/`1`, variants
in rows, haplotypes in columns - the convention
`kalis::CacheHaplotypes()` expects for a matrix source) into
fmalloc-backed, memory-mapped storage at one bit per call. The file
layout is locus-major: all haplotypes at one variant form a contiguous
bit row, and each row begins on a 64-byte boundary. This is the layout
kalis builds internally and the access pattern Li and Stephens
forward/backward kernels consume.

## Usage

``` r
fmalloc_haplotypes(x, runtime = NULL)

create_fmalloc_haplotypes(payload, dim)

# S3 method for class 'fmalloc_haplotypes'
dim(x)

# S3 method for class 'fmalloc_haplotypes'
print(x, ...)
```

## Arguments

- x:

  An integer, numeric, or logical matrix of haplotype calls, values `0`
  or `1` only (no missing calls: phased haplotypes do not have them, and
  neither does `kalis`'s own matrix input).

- runtime:

  Runtime handle from
  [`open_fmalloc()`](https://sounkou-bioinfo.github.io/Rfmalloc/Rfmalloc/reference/open_fmalloc.md);
  defaults to the runtime established by
  [`init_fmalloc()`](https://sounkou-bioinfo.github.io/Rfmalloc/Rfmalloc/reference/init_fmalloc.md).

- payload:

  An fmalloc raw vector created by the native haplotype buffer writer.

- dim:

  Integer dimensions `c(n_variant, n_haplotype)` for `payload`.

- ...:

  Unused.

## Value

An `fmalloc_haplotypes` object with `dim(x)`.

## Details

This is a SIBLING interface to the matmul
[fmalloc_tensor](https://sounkou-bioinfo.github.io/Rfmalloc/Rfmalloc/reference/fmalloc_tensor.md)
codec ABI, not an instance of it: haplotype hidden-Markov methods (Li
and Stephens local- ancestry inference) are not linear algebra, so
`fmalloc_haplotypes()` never registers a tensor codec and the resulting
object never participates in `%*%`. It shares the same fmalloc storage
substrate as
[`fmalloc_bed()`](https://sounkou-bioinfo.github.io/Rfmalloc/Rfmalloc/reference/fmalloc_bed.md)
and the typed tensors, with its own encode/decode pair.

The packed body is one bit per call, asymptotically thirty-two times
tighter than an integer `0`/`1` matrix and sixty-four times tighter than
doubles. Each locus is padded to a 64-byte boundary so an HMM kernel can
load donor words without repacking; that padding is visible for very
small panels.

## See also

[`fmalloc_hap_materialize()`](https://sounkou-bioinfo.github.io/Rfmalloc/Rfmalloc/reference/fmalloc_hap_materialize.md)
to decode back to a `0`/`1` matrix.

## Examples

``` r
rt <- open_fmalloc(tempfile(), size_gb = 0.1)
h <- matrix(c(0L, 1L, 1L, 0L, 1L, 0L), nrow = 3, ncol = 2)
hap <- fmalloc_haplotypes(h, runtime = rt)
fmalloc_hap_materialize(hap, runtime = rt)
#>      [,1] [,2]
#> [1,]    0    0
#> [2,]    1    1
#> [3,]    1    0
#> attr(,"class")
#> [1] "fmalloc_matrix" "matrix"         "fmalloc"        "integer"       
cleanup_fmalloc(rt)
```
