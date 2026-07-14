# Vendored pgenlib - provenance & update policy

The base subset under `packages/Rpgen/{configure,configure.ac,cleanup,
src/Makevars*,tools/}` is **generated**, not hand-maintained. It is produced by
[`vendorpgen.R`](vendorpgen.R) from one pinned input:

```
packages/Rpgen pgenlib base  =  (SHA-pinned pgenlibr 0.6.2 CRAN tarball)
                               - (pgenlibr's Rcpp-facing source files)
                               + (Rpgen Makevars and cleanup integration)
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
- All 468 files in pgenlibr's `tools/include/` manifest are byte-identical to
  stock pgenlibr 0.6.2. The separate `vendor-plink2-import` recipe adds the
  writer and program-level format-import closure on top of this base.

## Local integration

`src/Makevars.in` and `src/Makevars.win` each set

```make
OBJECTS = pvar.o pgenlibr.o RcppExports.o
```

naming pgenlibr's own R-level Rcpp bindings (`src/pgenlibr.cpp`,
`src/RcppExports.cpp`) plus its `.pvar` loader (`src/pvar.cpp`). Rpgen has
none of pgenlibr's Rcpp bindings - it does not depend on Rcpp at all - and
has its own hand-authored `src/rpgen.cpp` instead (not vendored, not
touched by this script; see `packages/Rpgen/inst/COPYRIGHTS`). `vendorpgen.R`
therefore rewrites that line to

```make
OBJECTS = rpgen.o rpgen_import.o rpgen_direct_sink.o rpgen_null_stream.o rpgen_plink2_glue.o
```

The recipe's generated Makevars also adds `pgenlib_write.cc`, the PLINK 2
program sources, and SFMT to `libPLINK2.a`, applies the CLI/direct-sink flags
only to vendored objects, and removes all generated archives and objects from
the clean target. The R-console, exit/abort, and direct-sink objects are part
of this recipe, so `vendorpgen.R check` drift-checks the complete Makevars.

pgenlibr's cleanup script only removes build products directly under `src/`.
Rpgen compiles the vendored sources in place under `tools/include/`, so the
recipe adds a bounded `find` which removes their `.o` files as well. This keeps
incremental development from silently linking stale generated objects.

Rpgen does not ship pgenlibr's `pvar.cpp`, `pvar.h`, `pgenlibr.cpp`, or
`RcppExports.cpp`. They are Rcpp-facing bindings, not pgenlib itself, and
including them would create an undeclared Rcpp dependency and break Windows
builds that compile every source file present.

## What's vendored vs. what's Rpgen's own

```
packages/Rpgen/
  configure, configure.ac              <- vendored, verbatim (autoconf: picks
                                           system vs. bundled zstd/libdeflate/simde)
  cleanup                              <- generated base plus external-object cleanup
  src/
    Makevars.in, Makevars.win          <- generated base plus Rpgen shim wiring
    Makevars.ucrt                      <- vendored, verbatim (`include Makevars.win`)
    rpgen.cpp                          <- Rpgen's own persistent readers
    rpgen_import.cpp                   <- Rpgen's own format-import drivers
    rpgen_cli_shim.h,
    rpgen_null_stream.c,
    rpgen_plink2_glue.{c,h}            <- Rpgen's own R-runtime adaptation
  tools/
    zstd_version.cpp                   <- vendored, verbatim (configure's
    libdeflate_version.cpp                compiler probes for a system install
    simde_version.cpp                     new enough to skip the bundled copy)
    include/                           <- vendored, verbatim, entire subtree
      include/*.cc,*.h                    (pgenlib core, read and writer paths)
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
