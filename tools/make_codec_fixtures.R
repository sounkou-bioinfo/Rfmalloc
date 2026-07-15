# Regenerates packages/Rgguf/inst/tinytest/fixtures/kquant_ggml_ref.rds:
# per quantized type, a payload produced by GGML's own quantizer
# (Rggml_quantize via Rllm) and the f32 values GGML's type-traits to_float
# decodes it back to. Rgguf's test_gguf_codec_ggml_ref.R pins its codec
# decoders to these bytes without needing Rggml at test time.
#
# Run from the repo root with the full stack installed:
#   Rscript tools/make_codec_fixtures.R
suppressMessages(library(Rllm))
rt <- Rfmalloc::open_fmalloc(tempfile(fileext = ".bin"))
on.exit(Rfmalloc::cleanup_fmalloc(rt), add = TRUE)
set.seed(42L)
n_elems <- 512L
x <- matrix(rnorm(n_elems, sd = 0.4), nrow = n_elems, ncol = 1L)
fix <- list()
for (ty in c("q4_0", "q4_1", "q8_0", "q2_0", "q2_k", "q4_k", "q6_k")) {
    Wt <- rllm_quantize_tensor(x, ty, runtime = rt)
    encoded <- unclass(Wt)
    payload <- raw(length(encoded))
    payload[] <- encoded
    fix[[ty]] <- list(
        payload = payload,
        expected = .Call("RC_rllm_dequantize", encoded, ty, n_elems,
                         PACKAGE = "Rllm"),
        n = n_elems
    )
}
out <- "packages/Rgguf/inst/tinytest/fixtures/kquant_ggml_ref.rds"
saveRDS(fix, out, version = 2)
cat("wrote ", out, " (", file.size(out), " bytes)\n", sep = "")
