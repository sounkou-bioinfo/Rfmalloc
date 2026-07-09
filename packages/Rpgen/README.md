
# Rpgen

<!-- badges: start -->

[![R-CMD-check](https://github.com/sounkou-bioinfo/Rfmalloc/actions/workflows/R-CMD-check.yaml/badge.svg)](https://github.com/sounkou-bioinfo/Rfmalloc/actions/workflows/R-CMD-check.yaml)
<!-- badges: end -->

Rpgen is a native PLINK 2 `.pgen` reader for R, and the genomics I/O arm
of the [Rfmalloc](https://github.com/sounkou-bioinfo/Rfmalloc) stack. It
vendors PLINK 2’s own `pgenlib` and exposes it through a **C-callable
API** so that other packages can read genotypes, dosages, phase and
multiallelic records straight from a `.pgen` in C, then pack them into
Rfmalloc’s file-backed genotype codecs (`bed` at 2 bits/genotype,
`dosage` at 1 byte, `haplotypes` at 1 bit) without a dense double copy.

## Why it exists

The CRAN package
[`pgenlibr`](https://cran.r-project.org/package=pgenlibr) also vendors
`pgenlib`, but it exposes only an R-level interface: it installs no
headers, declares no `LinkingTo`, and registers no `R_RegisterCCallable`
entry points. Nothing else can call its readers from C. Rpgen vendors
the same library and registers a C-callable surface (`Rpgen_open_info`,
and the genotype readers as they land), which is the whole point: the
Rfmalloc side reads `.pgen` in C and writes into memory-mapped storage,
with no round trip through an R matrix.

## Provenance and license

`pgenlib` is **LGPL-3**, so Rpgen is **GPL-3** (LGPL-3 combines up to
GPLv3, not the GPL (\>= 2) used elsewhere in the stack). The vendored
tree is **generated**, not hand-maintained:
`tools/vendor-pgenlib/vendorpgen.R` reproduces it from a pinned CRAN
`pgenlibr` source tarball (the same “own our path” recipe the stack uses
for GGML), and CI guards it against drift
(`vendored-pgenlib-matches-recipe`). See `inst/COPYRIGHTS` for the full
LGPL / MIT / BSD attribution of `pgenlib`, `zstd`, `libdeflate` and
`simde`.

## Usage

`rpgen_info()` opens a `.pgen` and reports its sample and variant
counts, straight from the header. This example runs against the small
`chr21_phase3_start.pgen` fixture that ships with the package:

``` r
pgen <- system.file("extdata", "chr21_phase3_start.pgen", package = "Rpgen")
rpgen_info(pgen)
#> $n_sample
#> [1] 2504
#> 
#> $n_variant
#> [1] 485
```

The counts are cross-checked against `pgenlibr` reading the same file in
the package’s own tests, so the native open sequence is pinned to the
reference implementation.

## Installation

From the [r-universe](https://sounkou-bioinfo.r-universe.dev):

``` r
install.packages("Rpgen",
  repos = c("https://sounkou-bioinfo.r-universe.dev", getOption("repos")))
```

or a GitHub subdir ref:

``` r
pak::pak("sounkou-bioinfo/Rfmalloc/packages/Rpgen")
```

Rpgen depends on `Rfmalloc`. It builds on Linux, macOS and Windows
(Rtools) and is checked on ARM (Apple Silicon and Linux aarch64) in CI.
