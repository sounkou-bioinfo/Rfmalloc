# Vendored plink2 VCF/BCF/BGEN/Oxford import closure - provenance & update policy

Milestone 4a's path to `VcfToPgen()`: reuse plink2's own importer instead of
writing a from-scratch htslib-based VCF reader, because the same vendored
closure also covers BCF/BGEN/Oxford for a later milestone - one codebase for
every genotype-file format plink2 itself imports, not one per format.

The vendored subset under `packages/Rpgen/tools/include/{plink2_*.cc,*.h}`
(program level) and `packages/Rpgen/tools/include/include/{pgenlib_write,SFMT}.{cc,c,h}`
(library level, joining `tools/vendor-pgenlib`'s pgenlib read subset) is
**generated**, not hand-maintained. It is produced entirely by
[`vendorplink2import.R`](vendorplink2import.R) from one pinned input:

```
tools/include/ VcfToPgen() closure  =  (SHA-pinned plink-ng commit tarball)
                                      + (one local patch: plink2_cmdline.cc logging -> R I/O)
```

Re-run and verify at any time:

```sh
Rscript tools/vendor-pgenlib/vendorpgen.R vendor              # 1. base pgenlib read subset + Makevars wiring (run first)
Rscript tools/vendor-plink2-import/vendorplink2import.R vendor  # 2. this tool's own files, on top
Rscript tools/vendor-plink2-import/vendorplink2import.R check   # assert the committed tree == this recipe
```

**Ordering matters.** `tools/vendor-pgenlib/vendorpgen.R vendor` wipes and
fully regenerates `packages/Rpgen/tools/include/` from the pgenlibr CRAN
tarball, which does not contain any of this tool's files - re-running it
after this tool has run will silently delete this tool's output. Always run
`vendorpgen.R` first if both need re-running; `vendorplink2import.R` only
ever adds/overwrites its own file list on top, never deletes anything
outside it, so it is always safe to run last.

`vendorpgen.R` also owns two lines in `packages/Rpgen/src/Makevars.in`/
`Makevars.win` that wire this tool's sources into the `libPLINK2.a` build
(`LIBPLINK2_SOURCES` gets `pgenlib_write.cc` + the program-level `plink2_*.cc`
files appended; a new `LIBPLINK2_C_SOURCES`/`LIBPLINK2_CO` pair is added for
`SFMT.c`, which is plain C). See `tools/vendor-pgenlib/vendorpgen.R`'s own
header comment ("edit 2") - this is the one piece of milestone 4a's wiring
that lives in that script rather than this one, because it touches the same
two files `vendorpgen.R` already generates and patches for its own `OBJECTS=`
edit.

## Base

- **plink-ng commit `a81e38220b16e3907bdcedbe6ce39b273e001e13`**
  (https://github.com/chrchang/plink-ng), fetched as GitHub's own per-commit
  tarball (`https://github.com/chrchang/plink-ng/archive/<sha>.tar.gz`),
  SHA256-pinned (`58e1bd94f7359acf160134d6ee80e0ea508c6cb1d8f8380a1b0b2d8ed1b1318f`)
  in `vendorplink2import.R`. This is the exact commit `tools/vendor-pgenlib`
  independently verified to have a byte-identical `2.0/include/pgenlib_read.cc`
  to Rpgen's existing vendored read subset (from pgenlibr 0.6.2) - i.e., the
  import closure vendored here is API-compatible with what Rpgen already has,
  confirmed rather than assumed.
- Unlike `tools/vendor-pgenlib` (which vendors from the CRAN package
  `pgenlibr`, itself a curated read-only subset of plink-ng), this tool
  vendors directly from the plink-ng monorepo, because the VCF/BCF/BGEN/
  Oxford import closure is program code (`plink2_import.cc`'s `VcfToPgen()`
  and friends) that pgenlibr never vendors - pgenlibr is a *reader* binding
  only.

## What's vendored, and why exactly these files

plink-ng's own tree is split into a *library* half (`2.0/include/` - pgenlib
proper, LGPL, what `tools/vendor-pgenlib` already takes the read subset of)
and a *program* half (`2.0/` - the plink2 CLI's own source, GPL-3, everything
that isn't under `include/`). Program-level files `#include` each other with
no `include/` path prefix, so they must land one directory level up from the
library subset to keep resolving: `packages/Rpgen/tools/include/` (mirrors
`2.0/`) vs. `packages/Rpgen/tools/include/include/` (mirrors `2.0/include/`,
where `tools/vendor-pgenlib`'s files already live).

Program level (`tools/include/`, new in milestone 4a):

  `plink2_import.cc`/`.h` - `VcfToPgen()`, `BcfToPgen()`, `OxGenToPgen()`,
    `OxBgenToPgen()`, `OxHapslegendToPgen()`, and friends. The entry point
    this milestone calls is `VcfToPgen()`; the others come along for free in
    the same translation unit and are exactly what a later milestone needs
    for BCF/BGEN/Oxford - "one codebase," not a VCF-only extraction.
  `plink2_common.cc`/`.h`, `plink2_cmdline.cc`/`.h` - shared plink2 program
    infrastructure: the "bigstack" arena (`g_bigstack_base`/`g_bigstack_end`,
    `InitBigstack()`), `ChrInfo` (`InitChrInfoHuman()`/`CleanupChrInfo()`),
    and logging (`logputs()`/`logerrputs()`/...). `plink2_cmdline.cc` carries
    this tool's one local patch, see "Local patch" below.
  `plink2_pvar.cc`/`.h`, `plink2_psam.cc`/`.h` - `.pvar`/`.psam` writers
    `VcfToPgen()` calls while producing its output trio.
  `plink2_compress_stream.cc`/`.h`, `plink2_decompress.cc`/`.h` - the zstd/
    gzip streaming wrappers plink2's own file I/O is written against.
  `plink2_import_legacy.cc`/`.h`, `plink2_random.cc`/`.h` - pulled in by
    `plink2_import.h`'s own `#include` graph (import_legacy: legacy-format
    scaffolding shared with the modern importers; random: the SFMT-backed
    PRNG some import paths use for tie-breaking, not cryptographic).
  `plink2_data.cc`/`.h`, `plink2_family.cc`/`.h` - **not** in the initial
    `#include` graph guess; discovered by actually compiling and linking the
    closure (see "How the closure was verified" below) - `VcfToPgen()` calls
    `WritePsam()`, `AppendChrsetLine()`, `DataFidColIsRequired()`,
    `DataSidColIsRequired()`, `DataParentalColsAreRequired()`,
    `AppendPhenoStrEx()`, and `ApplyHardCallThreshPhased()`, all defined in
    `plink2_data.cc`, which itself `#include`s `plink2_family.h`.
  Deliberately **not** vendored: `plink2.cc` itself (the CLI's `main()` and
    argv parser - milestone 4a's own driver, `src/rpgen_import.cpp`, replaces
    it entirely, see below), `plink2_merge.h` (only `SortMode`/`kSortNone`/
    etc., needed solely by `plink2.cc`'s own argv-to-flags translation, not
    by anything in the vendored closure - `rpgen_import_vcf()` passes
    `is_sortvars` as a plain `0`/`1` it decides itself), and every other
    `plink2_*.cc` in `2.0/` (glm, ld, adjust, matrix_calc, ...) - none of it
    is reachable from `VcfToPgen()`'s call graph.

Library level (`tools/include/include/`, joining the existing pgenlib read
subset):

  `pgenlib_write.cc`/`.h` - the `.pgen` writer (`STPgenWriter` and friends),
    LGPL like the read subset; `VcfToPgen()` writes through this, not the
    read-only path `tools/vendor-pgenlib` vendors.
  `SFMT.c`/`.h` - the SFMT PRNG `plink2_random.h` wraps. BSD-3-Clause
    (Mutsuo Saito, Makoto Matsumoto, Hiroshima University / University of
    Tokyo), not LGPL - see its own header for the verbatim license text.
    Plain C, not C++ (needs its own `LIBPLINK2_C_SOURCES`/`LIBPLINK2_CO`
    suffix rule in Makevars.in/.win - `SFMT.h` wraps its declarations in
    `extern "C"` for its C++ callers, so no name-mangling mismatch results).

## Local patch

`patches/plink2_cmdline.cc.patch` (unified diff, applied with
`patch -p1 -s -d tools/include/`, same mechanism `tools/vendor-ggml` uses for
its own patches): replaces five small logging functions
(`logputs_silent()`, `logputs()`, `logerrputs()`, `logputsb()`,
`logerrputsb()`) so they route through R's own `Rprintf()`/`REprintf()`
(declared by the minimal `<R_ext/Print.h>`, added by the same patch) instead
of a log `FILE*`/stdout/stderr. Two independent reasons this is required, not
optional:

  1. **Correctness.** Upstream's `logputs_silent()` unconditionally
     `fputs(str, g_logfile)`. `g_logfile` is only ever non-null if
     `InitLogfile()` (plink2.cc's own, never called here) opened one; Rpgen
     never does, so `g_logfile` stays `nullptr` for the process's whole
     lifetime, and upstream's version would segfault on the first log line
     `VcfToPgen()` emits.
  2. **CRAN/R-package policy.** Writing directly to `stdout`/`stderr` from
     compiled code bypasses R's own I/O (sinks, `capture.output()`, knitr,
     etc.); everything must go through `Rprintf()`/`REprintf()`.

Every other plink2 logging entry point (`logprintf()`, `logerrprintf()`,
`logprintfww()`, ...) is a `HEADER_INLINE` wrapper in `plink2_cmdline.h` that
bottoms out in one of these five functions, so patching only these five
covers the whole vendored tree - see the patch file itself and
`src/rpgen_import.cpp`'s top comment for the consequence: `VcfToPgen()` has
no error-message-buffer parameter, so a failure's human-readable explanation
reaches the user via `REprintf()` (R's console/stderr), while
`rpgen_import_vcf()`'s own `errbuf` carries only the numeric `PglErr` code.

## How the closure was verified

Every file listed above was actually compiled (`g++`/`gcc`, the same
`-DNO_UNALIGNED -DPGENLIB_NOPRINT` flags Rpgen's own Makevars use) and linked
together against system `libzstd`/`libdeflate` with a stub `main()` that
takes `VcfToPgen`'s address (forcing the linker to pull in and resolve every
symbol `plink2_import.o` references - a stronger check than only exercising
`VcfToPgen()` itself, since the object file also contains `BcfToPgen()`,
`OxBgenToPgen()`, and the rest, all of which must resolve too since they
share the same translation unit). This is how `plink2_data.cc`/
`plink2_family.cc` were discovered as necessary (the initial `#include`-graph
guess didn't have them - the linker did) and how `plink2_merge.h` was
confirmed unnecessary (nothing in the closure references `SortMode`).

## Arena and defaults (not part of the vendored files, but load-bearing)

`VcfToPgen()` allocates through plink2's process-global "bigstack" arena
(`g_bigstack_base`/`g_bigstack_end`, defined in `plink2_cmdline.cc`), which
nothing sets up automatically - `src/rpgen_import.cpp`'s `rpgen_import_vcf()`
carves it out via `plink2::InitBigstack()` before calling `VcfToPgen()` and
frees it again before returning (goto-cleanup style, matching `rpgen.cpp`'s
own convention). The arena is process-global and not reentrant: see that
file's top comment.

`rpgen_import_vcf()` skips all argv parsing (plink2.cc's own ~10,000-line
`main()` is not vendored) and instead passes the same default values a plain
`plink2 --vcf <path> --make-pgen` (no other flags) would compute - every
default in `src/rpgen_import.cpp`'s `VcfToPgen()` call has a comment citing
where in `plink2.cc` that default came from (grep that file for
`"VcfToPgen("` to see the real call site this mirrors).

## CLI shim (R CMD check "checking compiled code")

Vendoring the closure verbatim (above) left one gap: plink2 is a *program*,
not a library, so the vendored code still calls `printf()`/`exit()` and
writes through the raw `stdout`/`stderr` `FILE*`. `R CMD check --no-manual`
flags this as a WARNING ("checking compiled code", symbols `__printf_chk`/
`exit`/`stderr`/`stdout` in `libPLINK2.a`) - unacceptable for a
`--as-cran`/error-on-warning CI gate. Closed with a small, hand-maintained
shim, **not** part of `vendorplink2import.R`'s automated `vendor` action
(unlike `patches/plink2_cmdline.cc.patch`, which *is* applied by that
script): three new `packages/Rpgen/src/` files -
`rpgen_cli_shim.h` (the macro redirections: `printf` -> `Rprintf()`,
`stdout`/`stderr` -> a shared `/dev/null` `FILE*`, `exit`/`abort` -> a
longjmp relay), `rpgen_null_stream.c` (that shared bit-bucket `FILE*`,
opened once), and `rpgen_plink2_glue.c`/`.h` (the `jmp_buf` `exit()`/
`abort()` longjmp to, and the driver `rpgen_import_vcf()` in
`rpgen_import.cpp` `setjmp()`s before calling into the vendored closure -
see that header's top comment for why a bare `Rf_error()` from inside the
vendored code is not an acceptable substitute: it would longjmp past the
driver's own arena-free/file-close cleanup). `src/Makevars.in`/`.win` force
`rpgen_cli_shim.h` into every `LIBPLINK2`/`LIBPLINK2_CO` object via
`-include` (never into `rpgen.cpp`/`rpgen_import.cpp`/
`rpgen_null_stream.c`/`rpgen_plink2_glue.c`, which already use the R API,
or real `stdio.h`/`setjmp.h`, directly) - a target-specific `PKG_CPPFLAGS`
override built from a `PKG_CPPFLAGS_BASE` variable, using plain `=` rather
than `PKG_CPPFLAGS +=` (a GNU Make extension `R CMD check`'s "checking for
GNU extensions in Makefiles" step itself flags, and which would also be
circular here, since a target-specific `PKG_CPPFLAGS` referencing
`$(PKG_CPPFLAGS)` is a self-reference once that override is in scope).

**Consequence for re-vendoring**: this wiring lives directly in
`packages/Rpgen/src/Makevars.in`/`.win`, files `vendorpgen.R`'s `vendor`
action fully regenerates from the pgenlibr CRAN tarball (see that script's
own header comment, "edit"/"edit 2") and `vendorplink2import.R`'s `vendor`
action then patches (see this file's own top comment, "Ordering matters").
Neither script currently knows about the CLI-shim block - re-running either
one will silently drop it, and `R CMD check` will regress to the WARNING
above until the `OBJECTS =`/`PKG_CPPFLAGS_BASE`/target-specific-override
lines are re-added by hand (or `vendorpgen.R`/`vendorplink2import.R` are
themselves extended to re-apply this block, the same way they already own
the `LIBPLINK2_SOURCES`/`OBJECTS=` edits). The three new `.h`/`.c` files
themselves are untouched by re-vendoring either way (they are
hand-authored, like `rpgen_import.cpp`, not vendored).

## Update policy

Pinned at plink-ng `a81e38220b16e3907bdcedbe6ce39b273e001e13` until there is a
concrete reason to move (a `VcfToPgen()` bugfix this package needs, or a
`tools/vendor-pgenlib` pin bump that this tool should track so the two
subsets stay API-compatible - re-verify byte-identity of
`2.0/include/pgenlib_read.cc` against the pgenlibr-vendored copy before
bumping, the same check that justified this pin in the first place).
`vendorplink2import.R` is the template for whenever that happens: bump
`plinkng_sha`/`tarball_sha256`, re-run `vendor`, re-run `check`, re-verify the
closure still compiles+links+converts
(`packages/Rpgen/inst/tinytest/test_import_vcf.R`).
