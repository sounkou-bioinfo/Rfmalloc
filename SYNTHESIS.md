# Phase synthesis

This repository is an exploration of one idea: computation should close over
typed storage without forcing every representation through a dense R object.
The current synthesis is not a frozen API or a release boundary. It is the
smallest set of abstractions that survived contact with genomics, statistical
genetics, GGUF models, and out-of-core linear algebra.

## The abstraction that remains

There are three independent decisions:

1. A source reader emits bounded records with explicit semantics. A record may
   contain hardcalls, fixed-point dosages, phased haplotypes, packed bits, or
   doubles.
2. A storage destination owns allocation, lifetime, alignment, packing, and
   physical layout. It may own bytes in an fmalloc file or borrow a read-only
   span owned by another mapping.
3. A compute consumer chooses an algorithm appropriate to the semantics. Dense
   and compressed numeric tensors enter matrix contraction. Haplotype HMMs and
   banded LD algorithms use typed accessors instead.

The record-panel context is the boundary between the first two decisions. It
does not erase the difference between compressed and uncompressed data. It
makes that difference a destination choice instead of duplicating every reader
API. The storage-span interface is the dual boundary on the read side: a codec
or backend receives a pointer, byte extent, lifetime owner, and runtime context
without caring whether the bytes are owned by fmalloc or borrowed from GGUF.

The materialization rule is now simple: materialize only when the algorithm
requires a different representation, never merely because control crosses a
package boundary.

## What this resolves

- Rgguf no longer owns a partial C port of the GGUF format. Rggml vendors
  GGML's official `gguf.cpp`, and Rgguf is the R-facing adapter.
- Rllm weights borrow their exact encoded spans from the original read-only
  GGUF mapping. Loading a 4.9 GB model created 256 views over 4.79 GiB in 34 ms
  and created no persistent fmalloc allocation records.
- Rpgen keeps one persistent PGEN or BED reader and transfers bounded panels
  into hardcall, dosage, phased-haplotype, or dense destinations. PED/MAP,
  TPED/TFAM, BGEN, VCF/BCF, GEN, HAPS/legend, EIGENSTRAT, and legacy dosage
  retain PLINK2's own parsers, but `rpgen_ingest()` redirects every terminal
  `STPgenWriter` append shape to the same record sink. No genotype PGEN is
  serialized and decoded again. The file-producing `rpgen_import_*()` surface
  remains ordinary upstream PGEN output. PED/MAP keeps its bounded
  sample-major transpose scratch because the physical reordering is real;
  HAPS performs a bounded count pass because upstream supplies only a writer
  upper bound. Temporary metadata sidecars are cleaned on exit.
- Phased haplotypes are locus-major with a 64-byte-aligned row per variant.
  This matches the traversal of Li and Stephens and kalis-style HMM kernels.
  They remain a typed store, not a fake numeric matrix codec.
- A backend may decline a product. Correctness then falls back to bounded
  decode plus BLAS. Specialization changes speed, not meaning.

## Bets tested rather than protected

The current decimal ALP implementation is not a credible LLM weight format.
On a real 2048 by 2048 Q4_K weight, lossless ALP expanded model-like values to
64.2 bits per value and its scalar decode plus BLAS took 20.75 ms for batch 1.
Native GGML Q4_K used 4.5 bits per value and took 0.30 ms. ALP compressed a
three-decimal control to 7.1 bits per value, so the implementation remains
interesting for analytical decimal data. The LLM hypothesis now requires a
binary-float transform such as ALP-RD, a measured SIMD decoder, or a direct
compressed dot kernel. The benchmark is in `experiments/alp_gguf_cpu.R`.

The ds4 comparison exposes a different distinction. Its current SSD mode keeps
non-routed weights resident and uses a memory budget as a cache of complete
routed MoE experts, loading an expert from GGUF on a routing miss and preloading
known-hot experts. That cache policy is the central algorithm, not a property
of the allocator. fmalloc and borrowed mmap views solve storage, lifetime, and
page ownership; they do not schedule a model or predict sparse expert demand.
For a dense sequential model, `fmalloc_storage_advise()` can express prefetch
and release intentions, but a layer scheduler must produce the access sequence.
The fair experiment is therefore workload-specific: routing-aware expert cache
against an equivalent fmalloc cache for sparse MoE, and mmap plus advice against
`pread` double buffering for dense layer streaming, both with controlled cold
page-cache state. Cached view construction is not evidence about SSD
throughput. See [antirez/ds4](https://github.com/antirez/ds4#running-models-larger-than-ram)
for the design being compared.

## Threat model for this phase

The working environment is a trusted researcher running local code over local
datasets and model files in a controlled build. Inputs may still be truncated,
corrupt, or unexpectedly large, so bounds checks, integer-overflow checks,
mapping lifetime, decoder correctness, and deterministic cleanup are part of
correctness.

This phase does not assume a hostile multi-tenant service, adversarial package
loading, signed model distribution, sandbox escape resistance, or secrecy from
other processes owned by the same user. Work justified only by those deployment
threats is out of scope until such a deployment exists. If untrusted remote
files or a service boundary enter the project, the threat model must be written
again from that boundary.

## The next contradictions to push

1. Give HMM consumers direct bounded access to locus rows so kalis-class work
   does not require even an fmalloc double materialization.
2. Test ALP-RD and SIMD decode as independent storage-bandwidth experiments,
   then decide whether a fused compressed dot deserves to exist.
3. Build the layer access scheduler and compare mmap advice with explicit
   double buffering on cold storage.
4. Carry typed payloads into a CUDA backend without changing storage ownership
   or the decline contract.
5. Decide whether importer metadata should become its own semantic sink. It is
   currently transient because compute paths consume genotype records only.

No compatibility shim or API-version ceremony is warranted while every
consumer lives in this monorepo. When an abstraction changes, change all of its
consumers in the same commit and keep only the clearer form.
