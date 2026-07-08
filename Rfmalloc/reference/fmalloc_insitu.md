# In-place (by-reference) mutation of fmalloc vectors

Modify an fmalloc-backed atomic vector *in place*, writing straight
through the backing store and deliberately bypassing R's copy-on-modify.
`fmalloc_set()`/`fmalloc_fill()` are the by-reference analogue of
`x[i] <- value` / `x[] <- value`; `fmalloc_add()`/`fmalloc_sub()`/
`fmalloc_mul()`/`fmalloc_div()` compute `x <- x op y` in place (the
accumulate-into-`x` pattern iterative algorithms need), for numeric
vectors.

## Usage

``` r
fmalloc_set(x, i, value)

fmalloc_fill(x, value)

fmalloc_add(x, y)

fmalloc_sub(x, y)

fmalloc_mul(x, y)

fmalloc_div(x, y)
```

## Arguments

- x:

  An fmalloc-backed atomic vector (or matrix/array).

- i:

  Positive integer (1-based) linear indices to assign.

- value:

  For `fmalloc_set()`, a vector of length 1 (recycled) or `length(i)`.
  For `fmalloc_fill()`, a single scalar.

- y:

  For the arithmetic ops, a numeric scalar (recycled) or a vector of
  `length(x)`. `NA`/`NaN`/`Inf` follow IEEE double arithmetic (base R
  semantics).

## Value

`x`, invisibly, mutated in place.

## Details

On an *unshared* fmalloc vector, an ordinary `x[i] <- value` already
writes in place through the ALTREP data pointer - no copy - because the
file-backed storage is exposed directly and this package controls
duplication. The copy that hurts happens when the vector is *shared*
(`y <- x`): R then duplicates the whole payload to preserve value
semantics before modifying, which is catastrophic for a larger-than-RAM
or persistent vector. These functions mutate by reference regardless of
sharing - they never copy, updating the durable store directly and
returning the same object invisibly.

## Aliasing (read this)

Because there is no copy, all bindings to the same fmalloc vector
observe the change. After `y <- x; fmalloc_set(x, 1, 5)`, `y[1]` is also
`5` - `x` and `y` name the same backing store, whereas ordinary
`x[1] <- 5` would copy `x` (leaving `y` untouched). This breaks R's
usual value semantics *by design*; for a persistent runtime it is a
feature (the durable data is updated). For this reason mutation is only
ever done through these explicitly-named functions, never a silent `[<-`
method.

Supported for fixed-width atomic vectors (logical, integer, numeric,
complex, raw). Indices are 1-based linear positions (column-major for
matrices/arrays).

## Examples

``` r
if (FALSE) { # \dontrun{
rt <- open_fmalloc(tempfile(fileext = ".bin"))
x <- create_fmalloc_vector("numeric", 5, runtime = rt)
fmalloc_fill(x, 0)            # x[] <- 0, no copy
fmalloc_set(x, c(1, 3), 9)    # x[c(1,3)] <- 9, no copy
cleanup_fmalloc(rt)
} # }
```
