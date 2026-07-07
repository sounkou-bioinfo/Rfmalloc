#' (internal) q4_K x q8_K dot: runtime-dispatched variant vs scalar reference
#'
#' Not exported. Quantizes deterministic inputs of length \code{nblocks * 256}
#' to Q4_K/Q8_K and computes their dot product two ways: through the canonical
#' \code{ggml_vec_dot_q4_K_q8_K} (which the runtime SIMD dispatcher routes to
#' the staged AVX2/NEON variant where available) and through GGML's scalar
#' reference. The tinytest suite asserts the two agree, proving the staged ISA
#' variant is correct.
#'
#' @param nblocks Number of 256-element super-blocks.
#' @return Numeric length-2 vector \code{c(dispatched, scalar)}.
#' @keywords internal
rggml_test_q4k_dot <- function(nblocks = 4L) {
    .Call("RC_rggml_test_q4k_dot", as.integer(nblocks))
}
