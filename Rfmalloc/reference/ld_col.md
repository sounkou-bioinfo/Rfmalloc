# Neighbour run of one column of a banded LD store

Returns column `j`'s contiguous band of correlations: the decoded values
for rows `lo..hi` (the window around variant `j`), where `hi - lo + 1`
is the band length. This is the O(1) per-column access the LDpred2
banded matvec rides.

## Usage

``` r
ld_col(store, j)
```

## Arguments

- store:

  An
  [fmalloc_ld](https://sounkou-bioinfo.github.io/Rfmalloc/Rfmalloc/reference/fmalloc_ld.md)
  object.

- j:

  1-based column index.

## Value

A list with `lo` and `hi` (1-based inclusive row bounds of the band) and
`x` (the decoded correlations for rows `lo:hi`, length `hi - lo + 1`).

## See also

[`fmalloc_ld()`](https://sounkou-bioinfo.github.io/Rfmalloc/Rfmalloc/reference/fmalloc_ld.md),
[`ld_pair()`](https://sounkou-bioinfo.github.io/Rfmalloc/Rfmalloc/reference/ld_pair.md),
[`ld_ncol()`](https://sounkou-bioinfo.github.io/Rfmalloc/Rfmalloc/reference/ld_ncol.md)
