# (internal) q4_K x q8_K dot: runtime-dispatched variant vs scalar reference

Not exported. Quantizes deterministic inputs of length `nblocks * 256`
to Q4_K/Q8_K and computes their dot product two ways: through the
canonical `ggml_vec_dot_q4_K_q8_K` (which the runtime SIMD dispatcher
routes to the staged AVX2/NEON variant where available) and through
GGML's scalar reference. The tinytest suite asserts the two agree,
proving the staged ISA variant is correct.

## Usage

``` r
rggml_test_q4k_dot(nblocks = 4L)
```

## Arguments

- nblocks:

  Number of 256-element super-blocks.

## Value

Numeric length-2 vector `c(dispatched, scalar)`.
