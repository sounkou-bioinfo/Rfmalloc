library(tinytest)
library(Rllm)

# A small checkpoint-independent execution keeps every ESM semantic operator
# in the ordinary suite. The opt-in real-checkpoint test supplies the upstream
# numerical oracle.
program <- Rllm:::.rllm_esm2_program(
    n_layer = 1L, n_embd = 4L, n_head = 2L, n_ff = 8L, n_vocab = 6L,
    rms_eps = 1e-5, mask_id = 5L, padding_id = 1L,
    bos_id = 0L, eos_id = 2L, token_dropout = 0.12
)
set.seed(208L)
parameters <- lapply(program$parameters, function(parameter) {
    values <- rnorm(prod(parameter$shape), sd = 0.15)
    if (grepl("norm.*weight|layer_norm.*weight", parameter$name)) {
        values <- 1 + values
    }
    if (length(parameter$shape) > 1L) {
        dim(values) <- parameter$shape
    }
    values
})
tokens <- matrix(c(0L, 3L, 4L, 2L), ncol = 1L)
result <- rllm_execute(
    program,
    list(tokens = tokens, padding_mask = tokens == 1L),
    parameters = parameters
)

expect_equal(dim(result$logits), c(6L, 4L, 1L))
expect_equal(dim(result$representation), c(4L, 4L, 1L))
expect_equal(dim(result$attention.1), c(2L, 4L, 4L, 1L))
expect_equal(dim(result$contacts), c(2L, 2L, 1L))
expect_true(all(is.finite(result$logits)))
expect_true(all(is.finite(result$representation)))
expect_true(all(is.finite(result$contacts)))
expect_equal(result$contacts[, , 1L], t(result$contacts[, , 1L]))
for (head in 1:2) {
    expect_equal(
        rowSums(result$attention.1[head, , , 1L]),
        rep(1, 4), tolerance = 1e-12
    )
}

changed <- tokens
changed[[2L]] <- 4L
changed_result <- rllm_execute(
    program,
    list(tokens = changed, padding_mask = changed == 1L),
    parameters = parameters
)
expect_false(isTRUE(all.equal(changed_result$logits, result$logits)))

message("hermetic ESM-2 program execution tests completed")
