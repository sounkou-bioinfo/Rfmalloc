# Phased haplotypes as a 1-bit fmalloc store

Encodes an `L x N` matrix of phased haplotype calls (`0`/`1`, variants
in rows, haplotypes in columns - the convention
`kalis::CacheHaplotypes()` expects for a matrix source) into
fmalloc-backed, memory-mapped storage at one bit per call.

## Usage

``` r
fmalloc_haplotypes(x, runtime = NULL)

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

One bit per call is thirty-two times tighter than an integer `0`/`1`
matrix and sixty-four times tighter than the double matrix a naive
pipeline would build, so a haplotype panel that does not fit in RAM as
doubles still fits as a memory-mapped bitset.

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
