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

The materialization rule is simple: materialize only when the algorithm
requires a different representation, never merely because control crosses a
package boundary.

## What this resolves

- Rggml derives core, CPU, BLAS, GGUF, Vulkan and CUDA from one pinned official
  GGML v0.11.0 tree. The unused ggmlR engine fork and its split translation
  units are gone. Rgguf no longer owns a partial C port of the format; it is
  the R-facing adapter over the one official `gguf.cpp`.
- Rllm weights borrow their exact encoded spans from the original read-only
  GGUF mapping. Loading a 4.9 GB model created 256 views over 4.79 GiB in 34 ms
  and created no persistent fmalloc allocation records. CPU computation uses
  those spans directly. CUDA makes the one materialization the device actually
  requires: a model-owned execution context uploads the codec-native weights
  once and reuses them. Device residency is therefore context state, not a
  second storage format and not a compressed-versus-uncompressed API split.
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
  `Rfmalloc_haplotypes_data()` exposes the body, dimensions, meaningful row
  bytes and padded stride without decoding. This is the bit order and alignment
  kalis uses for its private `hap_locus` cache. A borrowed-cache method needs
  only a locus pointer table and an owning SEXP; PGEN, phased VCF/BCF and HAPS
  then reach kalis through Rpgen without another importer, integer matrix, or
  packed copy. Arbitrary haplotype subsetting still requires repacking because
  it changes bit positions; locus subsetting can remain pointer-only.
- A backend may decline a product. Correctness then falls back to bounded
  decode plus BLAS. Specialization changes speed, not meaning.

## Bets tested rather than protected

The decimal ALP implementation is not a credible LLM weight format.
On a real 2048 by 2048 Q4_K weight, lossless ALP expanded model-like values to
64.2 bits per value and its scalar decode plus BLAS took 20.75 ms for batch 1.
Native GGML Q4_K used 4.5 bits per value and took 0.30 ms. ALP compressed a
three-decimal control to 7.1 bits per value, so the implementation remains
interesting for analytical decimal data. The LLM hypothesis requires a
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

The CUDA experiment separates residency from reusable execution. Keeping
codec-native weights in a model-owned device context is a clear win and does
not alter the storage contract. Keeping GGML's graph allocator in a persistent
scheduler is not: on the RTX 5050, a 12-token prompt followed by 128 greedy
tokens measured 69.7 tok/s on the retained direct path and 63.7 with the
scheduler. CUDA graph capture measured 62.6 because attention extents and graph
properties change at each token. Both experiments were removed. Upstream
`llama-bench` on the same model and rig measures 156.1 tok/s without graphs and
460.1 with them, so the remaining contradiction is graph stability and mutable
KV residency, not another buffer-transfer or storage API.

## Architecture graphs are programs

The llama forward pass proves the storage and execution path, but its graph is
hand-written in C. Repeating that pattern for every architecture in llama.cpp
would turn Rllm into another architecture switch forest. The durable
abstraction should be a transparent, typed architecture language whose source
describes tensor roles, shape constraints, repeated blocks, persistent state,
and graph expressions. Native code should implement the operator vocabulary,
fused kernels, buffer movement and GGML lowering, not the identity of every
model family.

R is a useful surface syntax for this language, but arbitrary R closures would
make validation and compilation opaque. Constructors should produce a small
data AST. Its control forms are compile-time layer repetition, optional or tied
tensors, and shape expressions, not a general interpreter. Loading a GGUF file
normalizes its metadata and tensor directory, validates the architecture AST
with errors stated in model terms, and compiles it once to a typed native plan.
Execution instantiates that plan for a prompt or decode shape and backend. The
plan remains inspectable, serializable and testable as the source of truth.

[Rtinycc](https://github.com/sounkou-bioinfo/Rtinycc) is a plausible lowering
target for that plan, not the language itself. It can turn a declarative recipe
into C, compile and relocate it in memory, retain the live compiler state with
the callable, and recompile from the recipe after serialization. A validated
architecture AST could therefore emit one C graph-builder function rather than
interpret one R call per operator. The generated function should call a narrow,
opaque Rllm plan ABI or vtable instead of depending on GGML's entire header
surface. TinyCC does not need to optimize tensor arithmetic: the generated code
only assembles the graph, while official GGML kernels still perform the work.
The ordinary native plan interpreter remains the oracle and fallback; generated
C is a cache derived from the AST, never another source of truth.

The first proof is to express the existing llama graph in the language and
keep every pure-R, cache and CUDA oracle unchanged. The second proof must be an
architecture with real differences, such as optional QKV biases and a distinct
RoPE convention. If that model needs an architecture-specific C branch instead
of a reusable operator or state primitive, the vocabulary is not ready and
should be revised rather than escaped. A later MoE or state-space model tests
whether cache and routing state belong in the same semantic model. A constrained
vocabulary plus a deterministic validator makes both human and LLM-authored
programs reviewable; the prompt is not the artifact. This is the useful
discipline in [DSLs Enable Reliable Use of LLMs](https://martinfowler.com/articles/llm-and-dsls.html):
the semantic language and its validator become the maintained source of truth.
An optional Rtinycc proof must additionally produce the same graph and logits
as the interpreter, cross into native code once per graph build rather than once
per node, and demonstrate that compiler-state lifetime follows model lifetime.

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

1. Patch kalis to borrow the aligned haplotype view while preserving its cache
   ownership and SIMD invariants. Compare Forward/Backward output against its
   copied cache and assert that no integer matrix or second packed body exists.
2. Extract the llama graph into the typed architecture AST, compile it once,
   and require all existing CPU, CUDA and cache oracles to remain unchanged.
   Add one structurally different architecture only after that equivalence is
   exact.
3. Test ALP-RD and SIMD decode as independent storage-bandwidth experiments,
   then decide whether a fused compressed dot deserves to exist.
4. Build the layer access scheduler and compare mmap advice with explicit
   double buffering on cold storage.
5. Make a decode graph genuinely reusable before revisiting persistence. Test
   stable or bucketed attention extents and device-resident mutable KV state
   against the current host-authoritative cache, preserving explicit CPU/CUDA
   handoff. Persistent graph allocation without stable execution has already
   measured slower and does not deserve an API.
6. Decide whether importer metadata should become its own semantic sink. It is
   transient because compute paths consume genotype records only.

No compatibility shim or API-version ceremony is warranted while every
consumer lives in this monorepo. When an abstraction changes, change all of its
consumers in the same commit and keep only the clearer form.
