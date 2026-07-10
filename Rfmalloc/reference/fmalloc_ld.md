# Banded LD (correlation) matrix as a compressed fmalloc store

Stores a banded symmetric linkage-disequilibrium (Pearson correlation)
matrix in fmalloc-backed, memory-mapped storage as quantized integers.
This is a SIBLING interface to the matmul
[fmalloc_tensor](https://sounkou-bioinfo.github.io/Rfmalloc/Rfmalloc/reference/fmalloc_tensor.md)
codec ABI, not an instance of it (like
[`fmalloc_haplotypes()`](https://sounkou-bioinfo.github.io/Rfmalloc/Rfmalloc/reference/fmalloc_haplotypes.md)):
an LD matrix is read one column's neighbours at a time by a Gibbs
sampler or a ridge solve (LDpred2), never multiplied as a decoded dense
`p x p` double matrix, so the store is a typed accessor with its own
read API and never participates in `%*%`.

## Usage

``` r
fmalloc_ld(i, j, x, n_variants, bits = 8L, window = 0L, runtime = NULL)

# S3 method for class 'fmalloc_ld'
dim(x)

# S3 method for class 'fmalloc_ld'
print(x, ...)
```

## Arguments

- i, j:

  Integer vectors of 1-based row/column indices of the stored
  correlations (COO triplets). The band of column `j` is taken as the
  contiguous range spanning every `i` seen for that `j` (always
  including the diagonal); interior gaps are stored as `0`.

- x:

  An `fmalloc_ld` object.

- n_variants:

  Number of variants (the matrix is `n_variants` x `n_variants`).

- bits:

  Quantization width, `8` (int8, the default) or `16` (int16).

- window:

  Optional integer recording the build window (informational, stored in
  the header); `0` if unknown.

- runtime:

  Runtime handle from
  [`open_fmalloc()`](https://sounkou-bioinfo.github.io/Rfmalloc/Rfmalloc/reference/open_fmalloc.md);
  defaults to the runtime established by
  [`init_fmalloc()`](https://sounkou-bioinfo.github.io/Rfmalloc/Rfmalloc/reference/init_fmalloc.md).

- ...:

  Unused.

## Value

An `fmalloc_ld` object (a compressed, mmap-backed banded LD matrix).

## Details

The variants are assumed position-sorted, so each column `j`'s in-window
neighbours form a contiguous index range `[lo_j, hi_j]`. The store keeps
the full symmetric band per column (both `r[j, k]` and `r[k, j]`), the
diagonal is `1`, and no explicit neighbour indices are stored - the row
of a column's `t`-th stored value is `lo_j + t`. A per-column offset
table gives O(1) random access to any column's neighbour run and a
cache-friendly banded matvec.

Correlations are quantized to `bits`-wide integers: `r` in `[-1, 1]`
becomes `round(r * S)` clamped to `[-S, S]`, decoded back as `q / S`,
with `S = 127` for `bits = 8` (int8, resolution `~1/127`) or `S = 32767`
for `bits = 16` (int16, resolution `~3e-5`).

`fmalloc_ld()` builds a store from `(i, j, x)` correlation triplets;
RfmallocStatgen's `statgen_snp_cor()` builds one directly from a
genotype tensor. Read it with
[`ld_ncol()`](https://sounkou-bioinfo.github.io/Rfmalloc/Rfmalloc/reference/ld_ncol.md),
[`ld_pair()`](https://sounkou-bioinfo.github.io/Rfmalloc/Rfmalloc/reference/ld_pair.md)
and
[`ld_col()`](https://sounkou-bioinfo.github.io/Rfmalloc/Rfmalloc/reference/ld_col.md).

## See also

[`ld_ncol()`](https://sounkou-bioinfo.github.io/Rfmalloc/Rfmalloc/reference/ld_ncol.md),
[`ld_pair()`](https://sounkou-bioinfo.github.io/Rfmalloc/Rfmalloc/reference/ld_pair.md),
[`ld_col()`](https://sounkou-bioinfo.github.io/Rfmalloc/Rfmalloc/reference/ld_col.md)

## Examples

``` r
rt <- open_fmalloc(tempfile(), size_gb = 0.1)
## a 4-variant tridiagonal correlation matrix
i <- c(1, 2, 1, 2, 3, 2, 3, 4, 3, 4)
j <- c(1, 1, 2, 2, 2, 3, 3, 3, 4, 4)
x <- c(1, 0.5, 0.5, 1, 0.3, 0.3, 1, -0.2, -0.2, 1)
corr <- fmalloc_ld(i, j, x, n_variants = 4, runtime = rt)
ld_pair(corr, 1, 2)
#> [1] 0.503937
ld_col(corr, 2)
#> $lo
#> [1] 1
#> 
#> $hi
#> [1] 3
#> 
#> $x
#> [1] 0.5039370 1.0000000 0.2992126
#> 
cleanup_fmalloc(rt)
```
