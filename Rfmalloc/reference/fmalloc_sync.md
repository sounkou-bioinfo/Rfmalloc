# Flush an fmalloc runtime's backing store to disk

Writes to an fmalloc runtime (including in-place mutations via
[`fmalloc_set()`](https://sounkou-bioinfo.github.io/Rfmalloc/Rfmalloc/reference/fmalloc_insitu.md)
and friends) land in the OS page cache of the `MAP_SHARED` backing file.
They survive a normal process exit, but until the kernel writes dirty
pages back - which it does asynchronously - a crash or power loss can
lose unsynced data, with no atomicity. `fmalloc_sync()` forces the
durability barrier with `msync()` (and `fsync()`), so persistent data is
on disk when it returns.

## Usage

``` r
fmalloc_sync(runtime = NULL, wait = TRUE)
```

## Arguments

- runtime:

  Runtime handle from
  [`open_fmalloc()`](https://sounkou-bioinfo.github.io/Rfmalloc/Rfmalloc/reference/open_fmalloc.md);
  defaults to the runtime established by
  [`init_fmalloc()`](https://sounkou-bioinfo.github.io/Rfmalloc/Rfmalloc/reference/init_fmalloc.md).

- wait:

  If `TRUE` (default), block until the flush completes (`MS_SYNC`); if
  `FALSE`, schedule an asynchronous flush (`MS_ASYNC`).

## Value

The number of bytes flushed, invisibly.

## Details

This matters only for `persistent` runtimes where durability across an
unclean shutdown is required; `scratch` runtimes are ephemeral by
design. On platforms without `msync` (e.g. the Windows/Rtools toolchain)
this is a no-op and returns 0, relying on OS writeback.
