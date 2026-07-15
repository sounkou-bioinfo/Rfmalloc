library(tinytest)
library(Rllm)

contains_runtime_object <- function(x) {
    if (is.function(x) || is.environment(x) || typeof(x) == "externalptr") {
        return(TRUE)
    }
    is.list(x) && any(vapply(x, contains_runtime_object, logical(1)))
}

parameter <- function(name, shape) rllm_parameter(name, shape)

# ESM stresses padding-aware bidirectional attention, LayerNorm with bias,
# per-token representations, pooled representations and a pairwise contact
# output. The module body is ordinary R and the graph reads in pipe order.
d_model <- "d_model"
n_residue <- "n_residue"
norm_weight <- parameter("esm.norm.weight", d_model)
norm_bias <- parameter("esm.norm.bias", d_model)
q <- parameter("esm.attn.q.weight", c(d_model, d_model))
k <- parameter("esm.attn.k.weight", c(d_model, d_model))
v <- parameter("esm.attn.v.weight", c(d_model, d_model))
o <- parameter("esm.attn.o.weight", c(d_model, d_model))
ff_in <- parameter("esm.ff.in.weight", c(d_model, "ff_width"))
ff_out <- parameter("esm.ff.out.weight", c("ff_width", d_model))

esm_block <- rllm_module("esm.encoder.block", function(x) {
    x |>
        rllm_residual(function(branch) {
            branch |>
                rllm_norm(norm_weight, kind = "layer", bias = norm_bias) |>
                rllm_attention(
                    q, k, v, o, heads = 20L,
                    rope = list(type = "neox"),
                    mask = list(type = "padding", input = "padding_mask")
                )
        }) |>
        rllm_residual(function(branch) {
            branch |>
                rllm_norm(norm_weight, kind = "layer", bias = norm_bias) |>
                rllm_linear(ff_in) |>
                rllm_op("gelu") |>
                rllm_linear(ff_out)
        })
})

esm_sequence <- rllm_input(
    "residue_embedding", c(feature = d_model, sequence = n_residue)
) |>
    esm_block() |>
    rllm_tap("per_token")
esm_mean <- esm_sequence |>
    rllm_pool("mean") |>
    rllm_tap("mean")
esm_contact <- esm_sequence |>
    rllm_op(
        "contact_head", symmetric = TRUE,
        output_shape = c(row = n_residue, column = n_residue)
    ) |>
    rllm_tap("contacts")
esm <- rllm_program(
    list(sequence = esm_sequence, mean = esm_mean, contacts = esm_contact),
    "esm2"
)

expect_equal(names(esm$outputs), c("per_token", "mean", "contacts", "sequence"))
expect_true(all(c("attention", "layer_norm", "pool", "contact_head") %in%
                vapply(esm$nodes, `[[`, character(1), "op")))
expect_true(any(vapply(esm$nodes, function(node) {
    identical(node$module, "esm.encoder.block")
}, logical(1))))
expect_false(contains_runtime_object(esm))

# Evo 2 stresses an architecture-level schedule of short, medium and long
# stateful convolutions. The schedule is data on one reusable operator, not a
# family name hidden in C++.
hyena <- rllm_module("striped_hyena.operator", function(x, span) {
    x |>
        rllm_op(
            "hyena_convolution",
            span = span,
            causal = TRUE,
            state = list(kind = "convolution", width = span)
        )
})
evo <- rllm_input(
    "dna_embedding", c(feature = "hidden", sequence = "n_base")
)
for (span in c("short", "medium", "long")) evo <- hyena(evo, span)
evo <- evo |> rllm_tap("intermediate")
evo <- rllm_program(evo, "striped_hyena_2")
hyena_nodes <- Filter(function(node) node$op == "hyena_convolution", evo$nodes)

expect_equal(vapply(hyena_nodes, function(node) node$attributes$span,
                    character(1)), c("short", "medium", "long"))
expect_true(all(vapply(hyena_nodes, function(node) {
    identical(node$module, "striped_hyena.operator")
}, logical(1))))
expect_false(contains_runtime_object(evo))

# Tiny Recursive Models stress shared parameters, nested control flow and two
# carried states. rllm_loop() records each body once, so H and L remain
# symbolic and the shared cell keeps one module identity.
shared_weight <- parameter("recursive.cell.weight", c("hidden", "hidden"))
recursive_cell <- rllm_module("recursive.cell", function(state) {
    mixed <- rllm_op(
        state, "recursive_mlp", weight = shared_weight,
        output_shape = state$latent$shape
    )
    list(
        answer = rllm_op(
            list(answer = state$answer, update = mixed), "answer_update",
            output_shape = state$answer$shape
        ),
        latent = mixed
    )
})

puzzle <- rllm_input("puzzle", c(feature = "hidden"))
state <- list(
    answer = puzzle |> rllm_op("answer_init"),
    latent = puzzle |> rllm_op("latent_init")
)
state <- rllm_loop(state, "H", function(outer) {
    rllm_loop(outer, "L", recursive_cell, name = "low_level")
}, name = "high_level")
trm <- rllm_program(state, "tiny_recursive_model")

top_loop <- Filter(function(node) node$op == "loop", trm$nodes)[[1L]]
inner_loop <- Filter(
    function(node) node$op == "loop",
    top_loop$attributes$body$nodes
)[[1L]]
cell_nodes <- inner_loop$attributes$body$nodes
expect_equal(top_loop$attributes$times, "H")
expect_equal(inner_loop$attributes$times, "L")
expect_true(any(vapply(cell_nodes, function(node) {
    identical(node$module, "recursive.cell")
}, logical(1))))
expect_equal(names(inner_loop$attributes$body$parameters),
             "recursive.cell.weight")
expect_false(contains_runtime_object(trm))

roundtrip <- unserialize(serialize(trm, NULL))
expect_identical(roundtrip, trm)

leaky <- 1
attr(leaky, "builder") <- new.env(parent = emptyenv())
expect_error(
    rllm_op(puzzle, "opaque_attribute", payload = leaky),
    "data only"
)

foreign <- rllm_input("foreign", c(feature = "hidden"))
pair_module <- rllm_module("pair", function(x) x[[1L]])
expect_error(pair_module(list(puzzle, foreign)), "different programs")

message("R module and pipe program tests completed")
