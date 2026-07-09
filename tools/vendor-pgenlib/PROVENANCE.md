# Vendored pgenlib - provenance & update policy

The vendored subset under `packages/Rpgen/{configure,configure.ac,cleanup,
src/Makevars*,src/pvar.cpp,src/pvar.h,tools/}` is **generated**, not
hand-maintained. It is produced entirely by
[`vendorpgen.R`](vendorpgen.R) from one pinned input:

```
packages/Rpgen vendored subset  =  (SHA-pinned pgenlibr 0.6.2 CRAN tarball)
                                  + (one edited line: OBJECTS= in Makevars.in/.win)
```

Re-run and verify at any time:

```sh
Rscript tools/vendor-pgenlib/vendorpgen.R check     # assert the committed tree == this recipe
Rscript tools/vendor-pgenlib/vendorpgen.R vendor    # regenerate in place
```

This is what it means to **own our path to pgenlib**: the tree does not
depend on a git repository (chrchang/plink-ng is a large, actively-developed
mono-repo covering all of PLINK 2, most of it irrelevant to a read-only R
binding). The source of record is an immutable CRAN artifact, pinned here.

## Base

- **pgenlibr `0.6.2`** source tarball from CRAN
  (`pgenlibr_0.6.2.tar.gz`, sha256
  `e73520388a0d275d5af07befaa051bedef4172875d68f61f035f3e84275262bf`),
  pinned in `vendorpgen.R`. CRAN source tarballs are immutable and
  permanently archived, so this pin can never shift under us.
- pgenlibr is Christopher Chang's own CRAN package: the reference R binding
  for pgenlib, maintained by pgenlib's author. It already vendors exactly
  the pgenlib *read* subset (no writer) plus Zstd 1.5.5, libdeflate and
  SIMDe, and its `configure` already contains the logic to prefer system
  zstd/libdeflate/simde over the bundled copies when a recent-enough
  version is available via `pkg-config`. That build - CRAN-accepted,
  exercised across CRAN's whole build-machine matrix - is Rpgen's
  reference, not a rewrite of it.
- 466 of the 468 files under `tools/include/` are byte-identical to stock
  pgenlibr 0.6.2 (the whole subtree - `tools/include/include/` for pgenlib
  itself, plus the bundled `zstd/`, `libdeflate/`, `simde/`); the other 2
  files (`src/Makevars.in`, `src/Makevars.win`) carry the one local edit
  below.

## The one local edit

`src/Makevars.in` and `src/Makevars.win` each set

```make
OBJECTS = pvar.o pgenlibr.o RcppExports.o
```

naming pgenlibr's own R-level Rcpp bindings (`src/pgenlibr.cpp`,
`src/RcppExports.cpp`) plus its `.pvar` loader (`src/pvar.cpp`). Rpgen has
none of pgenlibr's Rcpp bindings - it does not depend on Rcpp at all - and
has its own hand-authored `src/rpgen.cpp` instead (not vendored, not
touched by this script; see `packages/Rpgen/inst/COPYRIGHTS`). `vendorpgen.R`
therefore rewrites that one line to

```make
OBJECTS = rpgen.o
```

Applied as a direct line substitution inside `vendorpgen.R` (see the
`src_files_patched` loop) rather than as a `patches/*.patch` file the way
`tools/vendor-ggml/` does it: a single deterministic key=value rewrite does
not need a diff format, and keeping it inline keeps the one edit and its
rationale in the same place, next to the pin.

`src/pvar.cpp` and `src/pvar.h` are still vendored byte-identical (for
provenance, and as a future milestone's starting point for variant
metadata), just not compiled: they `#include <Rcpp.h>` and use
`Rcpp::String`/`Rcpp::stop()` throughout, which only matters if something
puts them in `OBJECTS`. Since `OBJECTS` is set explicitly, R's build never
falls back to compiling every `.cpp` it finds in `src/`, so their presence
is inert until a later milestone opts them in (and adds Rcpp to
`LinkingTo`/`Imports` at that point).

Everything else - `PKG_CPPFLAGS`, `PKG_LIBS`, the `ZSTD_SOURCES`/
`LIBDEFLATE_SOURCES`/`LIBPLINK2_SOURCES` lists, the `libPLINK2.a` build
rule, `configure`/`configure.ac`'s system-vs-bundled zstd/libdeflate/simde
detection - is untouched, because Rpgen's `tools/include/` directory
layout is structurally identical to pgenlibr's own (`../tools/include`
relative to `src/`), so no path in the Makevars templates or the autoconf
script needed to change.

## What's vendored vs. what's Rpgen's own

```
packages/Rpgen/
  configure, configure.ac, cleanup     <- vendored, verbatim (autoconf: picks
                                           system vs. bundled zstd/libdeflate/simde)
  src/
    Makevars.in, Makevars.win          <- vendored, OBJECTS= edited (see above)
    Makevars.ucrt                      <- vendored, verbatim (`include Makevars.win`)
    pvar.cpp, pvar.h                   <- vendored, verbatim, NOT compiled (see above)
    rpgen.cpp                          <- Rpgen's own; the whole point of the package
  tools/
    zstd_version.cpp                   <- vendored, verbatim (configure's
    libdeflate_version.cpp                compiler probes for a system install
    simde_version.cpp                     new enough to skip the bundled copy)
    include/                           <- vendored, verbatim, entire subtree
      include/*.cc,*.h                    (pgenlib/plink2 core, read path only)
      zstd/**                             (bundled Zstd 1.5.5, BSD)
      libdeflate/**                       (bundled libdeflate, MIT)
      simde/**                            (bundled SIMDe, MIT)
  inst/
    extdata/chr21_phase3_start.{pgen,pvar.zst,psam}
                                        <- vendored, verbatim (pgenlibr's own
                                           test fixture; oracle input for
                                           inst/tinytest/test_open.R)
    include/Rpgen.h                    <- Rpgen's own (installed C-callable header)
    COPYRIGHTS                         <- Rpgen's own (this package's license notice)
  R/, NAMESPACE, man/, DESCRIPTION,
  NEWS.md, tests/                      <- Rpgen's own
```

## Why GPL-3, not GPL (>= 2)

pgenlib is LGPL (>= 3). The LGPL-3 "combine into one work" permission is
written against GPLv3's compatibility terms specifically, not GPLv2's, so a
work that statically links pgenlib is license-compatible only up through
GPLv3. Every other package in this monorepo is GPL (>= 2) because none of
them vendor any LGPL code; Rpgen is the first to do so, hence GPL-3. See
`packages/Rpgen/inst/COPYRIGHTS` for the full notice and the verbatim
upstream LGPL/MIT/BSD license texts.

## Update policy

The vendored core stays at pgenlibr 0.6.2 until there is a concrete reason
to move (a pgenlib bugfix Rpgen needs, or a CRAN policy change pgenlibr's
maintainer adapts to that Rpgen should track). `vendorpgen.R` is the
template for whenever that happens: bump `pgenlibr_ver`/`pgenlibr_sha`,
re-run `vendor`, re-run `check`, re-validate
`inst/tinytest/test_open.R` against the (possibly also-updated) pgenlibr
CRAN release used as its oracle.
