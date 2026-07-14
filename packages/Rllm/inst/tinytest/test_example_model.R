library(tinytest)
library(Rllm)

message("Testing the hermetic README model...")

local({
    path <- system.file("extdata", "tiny-byte-model.gguf", package = "Rllm",
                        mustWork = TRUE)
    backing <- tempfile(fileext = ".bin")
    rt <- Rfmalloc::open_fmalloc(backing, mode = "scratch", size_gb = 0.1)
    on.exit({
        Rfmalloc::cleanup_fmalloc(rt)
        unlink(backing)
    }, add = TRUE)

    model <- rllm_gguf_model(path, runtime = rt)
    prompt <- charToRaw("The capital of Germany is")
    expect_identical(rllm_decode(model, rllm_encode(model, prompt)), prompt)

    gen <- rllm_generate(model, prompt, n_new = 8L, runtime = rt)
    expect_identical(gen$new_ids, rep.int(0x21L, 8L))
    expect_identical(gen$raw, charToRaw("!!!!!!!!"))
})

message("Hermetic README model tests completed")
