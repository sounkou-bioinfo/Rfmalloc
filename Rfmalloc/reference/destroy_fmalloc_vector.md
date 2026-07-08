# Explicitly destroy a fmalloc vector

Releases runtime bookkeeping for a single fmalloc ALTREP vector
immediately. In scratch mode, payload memory is immediately reclaimed.
In persistent mode, the vector payload is retained by default so
existing on-disk state remains durable; optional `unsafe = TRUE`
reclaims payload memory and marks metadata as non-recoverable.

## Usage

``` r
destroy_fmalloc_vector(x, unsafe = FALSE)
```

## Arguments

- x:

  Fmalloc ALTREP vector to destroy.

- unsafe:

  Whether to physically free persistent payload bytes. Unsafe destroy is
  intended for short-lived scratch-like cleanup and will mark the
  catalog entry as non-recoverable.

## Value

Logical value indicating whether a live vector was destroyed.

## Details

Explicit destroy fails when a vector is still referenced by another
fmalloc list vector as a child.

## Examples

``` r
if (FALSE) { # \dontrun{
rt <- open_fmalloc(tempfile(fileext = ".bin"), mode = "persistent")
v <- create_fmalloc_vector("integer", 10, runtime = rt)
destroy_fmalloc_vector(v)
} # }
```
