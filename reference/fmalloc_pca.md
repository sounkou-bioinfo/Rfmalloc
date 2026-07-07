# Out-of-core PCA / truncated SVD for fmalloc matrices

Principal component analysis of a large, file-backed fmalloc matrix,
computed from the Gram matrix: `G = X'X` via out-of-core
[`crossprod()`](https://rdrr.io/r/base/crossprod.html), a truncated
eigendecomposition of the (small) `n x n` `G`, and the scores `X V` via
out-of-core `%*%`. Every heavy step — the Gram matrix and the projection
— dispatches through the pluggable matrix-multiply backend (see
[fmalloc_backend](https://sounkou-bioinfo.github.io/Rfmalloc/reference/fmalloc_backend.md)),
so the same call runs on CPU BLAS today and on a registered GPU backend
unchanged. `X` may exceed RAM: the Gram matrix and projection stream
column tiles with a bounded resident set.

## Usage

``` r
fmalloc_pca(X, k = 10L, center = TRUE)
```

## Arguments

- X:

  An fmalloc-backed numeric matrix (`m` observations x `n` features).

- k:

  Number of principal components to return.

- center:

  Logical; center the columns (applied implicitly as a rank-1 correction
  to the Gram matrix and scores, so `X` is never copied).

## Value

A list with prcomp-like elements: `sdev` (component standard
deviations), `rotation` (`n x k` loadings), `x` (`m x k` scores), and
`center` (the column means, or `FALSE`).

## Details

This is efficient when the number of features `n` is moderate (e.g.
after highly-variable-feature selection with
[`fmalloc_colVars()`](https://sounkou-bioinfo.github.io/Rfmalloc/reference/fmalloc_colVars.md));
the `n x n` Gram matrix and its eigendecomposition are formed in memory.

## See also

[`fmalloc_colVars()`](https://sounkou-bioinfo.github.io/Rfmalloc/reference/fmalloc_colVars.md),
[fmalloc_backend](https://sounkou-bioinfo.github.io/Rfmalloc/reference/fmalloc_backend.md)
