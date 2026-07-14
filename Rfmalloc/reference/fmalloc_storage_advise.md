# Give the pager an access hint for typed storage

Applies a best-effort access hint to an fmalloc tensor payload or
borrowed storage view. `"sequential"` asks the pager to read ahead,
`"willneed"` asks it to prefetch, and `"dontneed"` releases fully
covered pages after a phase has consumed them. Unsupported platforms
treat the hint as a no-op. The function never changes the tensor bytes.

## Usage

``` r
fmalloc_storage_advise(
  x,
  advice = c("sequential", "willneed", "dontneed"),
  offset = 0,
  nbytes = NULL
)
```

## Arguments

- x:

  An `fmalloc_tensor` or its raw fmalloc payload.

- advice:

  One of `"sequential"`, `"willneed"`, or `"dontneed"`.

- offset:

  Byte offset into the payload.

- nbytes:

  Number of bytes to advise. `NULL` covers the rest of the payload.

## Value

`x`, invisibly.

## Details

This is deliberately a storage primitive rather than an LLM policy. A
graph scheduler, HMM, or out-of-core matrix algorithm can express its
own access phases over the same mapped spans.
