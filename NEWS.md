# NEWS

## Development (unreleased)

- Implemented nested fmalloc list persistence by reference for persistent runtimes.
- Added recursive list/container serialized-reference recovery in ALTREP
  serialization/unserialization.
- Clarified that full view-based subset support remains experimental (current
  behavior is subset-copy with copy-on-write duplication controls).
- Documented Simon Urbanek custom allocator PoC separately from Rfmalloc's prior
  allocator implementation in README references.
