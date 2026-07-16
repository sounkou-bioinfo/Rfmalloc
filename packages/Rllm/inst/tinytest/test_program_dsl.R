library(tinytest)
library(Rllm)

contains_runtime_object <- function(x) {
    if (is.function(x) || is.environment(x) || typeof(x) == "externalptr") {
        return(TRUE)
    }
    is.list(x) && any(vapply(x, contains_runtime_object, logical(1)))
}

parameter <- function(name, shape) rllm_parameter(name, shape)

# ESM-2 8M is a complete six-layer topology. It forces the AST to preserve
# typed token and padding inputs, attention as a genuine two-result operator,
# per-layer representations and attention maps, tied embeddings and a contact
# head over attention maps. The official contact head never consumes hidden
# representations.
esm_layers <- 6L
esm <- Rllm:::.rllm_esm2_program(
    n_layer = esm_layers, n_embd = 320L, n_head = 20L, n_ff = 1280L,
    n_vocab = 33L, rms_eps = 1e-5, mask_id = 32L, padding_id = 1L,
    bos_id = 0L, eos_id = 2L, token_dropout = 0.12
)

esm_ops <- vapply(esm$nodes, `[[`, character(1), "op")
expect_true(all(c("esm_token_embedding", "attention", "op_result",
                  "tied_projection", "esm_contact_head") %in% esm_ops))
expect_equal(sum(esm_ops == "attention"), esm_layers)
expect_equal(sum(esm_ops == "op_result"), 2L * esm_layers)
expect_equal(length(esm$parameters), 106L)
expect_equal(length(grep("^representation\\.", names(esm$outputs))),
             esm_layers + 2L)
expect_equal(length(grep("^attention\\.", names(esm$outputs))), esm_layers)
expect_equal(length(Filter(function(node) {
    identical(node$op, "input")
}, esm$nodes)), 2L)
expect_false(contains_runtime_object(esm))

# Evo 2 7B is a 32-layer schedule, not three generic convolutions. Its HCS
# and HCM blocks carry a width-3 projection FIR plus width-7 or width-128
# grouped FIR state. HCL carries the same projection FIR plus a 16-state IIR.
# Five causal attention layers are interleaved with those 27 Hyena cascades.
evo_hidden <- 4096L
evo_inner <- 11008L
evo_vocab <- 512L
evo_shape <- c(feature = evo_hidden, sequence = "n_base", batch = "batch")
evo_inputs <- rllm_inputs(
    tokens = c(sequence = "n_base", batch = "batch"),
    padding_mask = c(sequence = "n_base", batch = "batch"),
    .dtype = c(tokens = "i32", padding_mask = "bool"),
    .name = "evo2_7b"
)
evo_embedding <- parameter("embedding_layer.weight",
                           c(vocabulary = evo_vocab, feature = evo_hidden))
evo_hidden_state <- rllm_op(
    evo_inputs, "token_embedding", weight = evo_embedding,
    output_shape = evo_shape
)

evo_schedule <- rep(NA_character_, 32L)
evo_schedule[c(3, 7, 10, 14, 17, 21, 24, 28, 31)] <- "hcl"
evo_schedule[c(2, 6, 9, 13, 16, 20, 23, 27, 30)] <- "hcm"
evo_schedule[c(1, 5, 8, 12, 15, 19, 22, 26, 29)] <- "hcs"
evo_schedule[c(4, 11, 18, 25, 32)] <- "attention"
stopifnot(!anyNA(evo_schedule))

evo_norm <- function(x, name) {
    rllm_op(
        x, "rms_norm", weight = parameter(name, evo_hidden),
        eps = 1e-6, convention = "vortex"
    )
}
evo_mlp <- function(x, prefix, index) {
    rllm_op(
        x, "gated_mlp",
        gate = parameter(paste0(prefix, "mlp.l1.weight"),
                         c(evo_hidden, evo_inner)),
        up = parameter(paste0(prefix, "mlp.l2.weight"),
                       c(evo_hidden, evo_inner)),
        down = parameter(paste0(prefix, "mlp.l3.weight"),
                         c(evo_inner, evo_hidden)),
        activation = if (index == 1L) "gelu" else "identity"
    )
}
evo_add <- function(residual, update) {
    rllm_op(list(residual = residual, update = update), "add")
}

evo_block <- function(index, kind, padding_mask) {
    prefix <- paste0("blocks.", index - 1L, ".")
    pre_norm_name <- paste0(prefix, "pre_norm.scale")
    post_norm_name <- paste0(prefix, "post_norm.scale")

    rllm_module(paste0("striped_hyena.block.", index), function(x) {
        normalized <- evo_norm(x, pre_norm_name)
        if (kind == "attention") {
            update <- rllm_op(
                list(hidden = normalized, padding_mask = padding_mask),
                "attention",
                qkv = parameter(paste0(prefix, "inner_mha_cls.Wqkv.weight"),
                                c(evo_hidden, 3L * evo_hidden)),
                output = parameter(paste0(prefix, "inner_mha_cls.out_proj.weight"),
                                   c(evo_hidden, evo_hidden)),
                output_bias = parameter(
                    paste0(prefix, "inner_mha_cls.out_proj.bias"), evo_hidden
                ),
                heads = 32L, rope = list(type = "neox", base = 10000),
                mask = list(type = "causal"),
                state = list(kind = "kv", layout = "layer_head_sequence")
            )
        } else {
            projected <- rllm_linear(
                normalized,
                parameter(paste0(prefix, "projections.weight"),
                          c(evo_hidden, 3L * evo_hidden))
            )
            inner_width <- if (kind == "hcs") 7L else
                if (kind == "hcm") 128L else NULL
            groups <- if (kind == "hcl") evo_hidden else 256L
            update <- rllm_op(
                list(projected = projected, padding_mask = padding_mask),
                "hyena_cascade",
                variant = if (kind == "hcl") "long_iir" else
                    paste0(if (kind == "hcs") "short" else "medium", "_fir"),
                short_filter = parameter(
                    paste0(prefix, "filter.short_filter_weight"),
                    c(channel = 3L * evo_hidden, group = 1L, width = 3L)
                ),
                inner_filter = if (!is.null(inner_width)) parameter(
                    paste0(prefix, "filter.h"),
                    c(group = groups, singleton = 1L, width = inner_width)
                ) else NULL,
                direct = if (is.null(inner_width) || inner_width >= 128L) {
                    parameter(paste0(prefix, "filter.D"), evo_hidden)
                } else NULL,
                log_poles = if (kind == "hcl") parameter(
                    paste0(prefix, "filter.log_poles"),
                    c(group = groups, state = 16L, singleton = 1L)
                ) else NULL,
                residues = if (kind == "hcl") parameter(
                    paste0(prefix, "filter.residues"),
                    c(group = groups, state = 16L)
                ) else NULL,
                groups = groups,
                interleave = TRUE,
                state = if (kind == "hcl") {
                    list(fir = list(width = 2L),
                         iir = list(groups = groups, width = 16L))
                } else {
                    list(fir = list(width = 2L),
                         inner_fir = list(groups = groups,
                                          width = inner_width - 1L))
                },
                output_shape = evo_shape
            )
            update <- rllm_linear(
                update,
                parameter(paste0(prefix, "out_filter_dense.weight"),
                          c(evo_hidden, evo_hidden)),
                parameter(paste0(prefix, "out_filter_dense.bias"), evo_hidden)
            )
        }
        hidden <- evo_add(x, update)
        mlp <- evo_mlp(evo_norm(hidden, post_norm_name), prefix, index)
        if (index == 29L) mlp <- rllm_tap(mlp, "blocks.28.mlp.l3")
        evo_add(hidden, mlp)
    })
}

for (index in seq_along(evo_schedule)) {
    evo_hidden_state <- evo_block(
        index, evo_schedule[[index]], evo_inputs$padding_mask
    )(evo_hidden_state)
}
evo_hidden_state <- evo_norm(evo_hidden_state, "norm.scale")
evo_logits <- rllm_op(
    evo_hidden_state, "tied_projection", weight = evo_embedding,
    transpose = TRUE,
    output_shape = c(feature = evo_vocab, sequence = "n_base",
                     batch = "batch")
)
evo <- rllm_program(evo_logits, "evo2_7b")
hyena_nodes <- Filter(function(node) node$op == "hyena_cascade", evo$nodes)
attention_nodes <- Filter(function(node) node$op == "attention", evo$nodes)

expect_equal(vapply(hyena_nodes, function(node) node$attributes$variant,
                    character(1)),
             c("short_fir", "medium_fir", "long_iir")[
                 match(evo_schedule[evo_schedule != "attention"],
                       c("hcs", "hcm", "hcl"))
             ])
expect_equal(length(hyena_nodes), 27L)
expect_equal(length(attention_nodes), 5L)
expect_equal(sum(vapply(evo$nodes, function(node) node$op == "loop",
                        logical(1))), 0L)
expect_true("blocks.28.mlp.l3" %in% names(evo$outputs))
expect_false(contains_runtime_object(evo))

# TRM carries z_H and z_L through fixed evaluation-time improvement steps.
# Within each step, L_cycles update z_L from z_H + x, then one update of z_H
# consumes z_L. The same two-layer reasoning module performs both updates and
# is shared across every H, L and improvement iteration.
hidden <- "hidden"
sequence <- "sequence"

reasoning_blocks <- lapply(seq_len(2L), function(index) {
    prefix <- paste0("trm.reasoning.", index - 1L, ".")
    qkv <- parameter(paste0(prefix, "attn.qkv.weight"),
                     c(hidden, "qkv_width"))
    attn_out <- parameter(paste0(prefix, "attn.output.weight"),
                          c(hidden, hidden))
    gate_up <- parameter(paste0(prefix, "mlp.gate_up.weight"),
                         c(hidden, "2 * intermediate"))
    down <- parameter(paste0(prefix, "mlp.down.weight"),
                      c("intermediate", hidden))
    rllm_module(paste0("trm.reasoning.block.", index), function(x) {
        x <- x |>
            rllm_residual(function(branch) {
                branch |>
                    rllm_op(
                        "bidirectional_attention", qkv = qkv,
                        output = attn_out,
                        rope = list(type = "neox", base = 10000)
                    )
            }) |>
            rllm_op("rms_norm", eps = 1e-5)
        x |>
            rllm_residual(function(branch) {
                branch |>
                    rllm_op("swiglu", gate_up = gate_up, down = down)
            }) |>
            rllm_op("rms_norm", eps = 1e-5)
    })
})

reasoning_level <- rllm_module("trm.reasoning", function(pair) {
    value <- rllm_op(pair, "add")
    for (block in reasoning_blocks) value <- block(value)
    value
})

input_embeddings <- rllm_input(
    "input_embeddings", c(feature = hidden, sequence = sequence)
)
h_init <- parameter("trm.H_init", hidden)
l_init <- parameter("trm.L_init", hidden)
state <- list(
    z_H = input_embeddings |>
        rllm_op("broadcast_initial", value = h_init),
    z_L = input_embeddings |>
        rllm_op("broadcast_initial", value = l_init),
    input = input_embeddings
)

state <- rllm_loop(state, "halt_max_steps", function(improvement) {
    rllm_loop(improvement, "H_cycles", function(high) {
        low <- rllm_loop(high, "L_cycles", function(low) {
            injection <- rllm_op(
                list(high = low$z_H, input = low$input), "add"
            )
            list(
                z_H = low$z_H,
                z_L = reasoning_level(list(
                    hidden = low$z_L, injection = injection
                )),
                input = low$input
            )
        }, name = "low_cycle")
        list(
            z_H = reasoning_level(list(
                hidden = low$z_H, injection = low$z_L
            )),
            z_L = low$z_L,
            input = low$input
        )
    }, name = "high_cycle")
}, name = "improvement_step")

lm_head <- parameter("trm.lm_head.weight", c(hidden, "vocab"))
q_weight <- parameter("trm.q_head.weight", c(hidden, 2L))
q_bias <- parameter("trm.q_head.bias", 2L)
answer <- state$z_H |>
    rllm_linear(lm_head) |>
    rllm_op("drop_puzzle_prefix", count = "puzzle_emb_len")
halt <- state$z_H |>
    rllm_op("select_first_token", output_shape = c(feature = hidden)) |>
    rllm_linear(q_weight, q_bias)
trm <- rllm_program(
    list(answer = answer, halt = halt, z_H = state$z_H, z_L = state$z_L),
    "tiny_recursive_model"
)

improvement_loop <- Filter(function(node) node$op == "loop", trm$nodes)[[1L]]
high_loop <- Filter(
    function(node) node$op == "loop",
    improvement_loop$attributes$body$nodes
)[[1L]]
low_loop <- Filter(
    function(node) node$op == "loop",
    high_loop$attributes$body$nodes
)[[1L]]
reasoning_nodes <- low_loop$attributes$body$nodes
expect_equal(improvement_loop$attributes$times, "halt_max_steps")
expect_equal(high_loop$attributes$times, "H_cycles")
expect_equal(low_loop$attributes$times, "L_cycles")
expect_true(any(vapply(reasoning_nodes, function(node) {
    identical(node$module,
              c("trm.reasoning", "trm.reasoning.block.1"))
}, logical(1))))
expect_equal(
    sum(names(trm$parameters) == "trm.reasoning.0.attn.qkv.weight"),
    1L
)
expect_true(all(c("trm.H_init", "trm.L_init", "trm.lm_head.weight",
                  "trm.q_head.weight", "trm.q_head.bias") %in%
                names(trm$parameters)))
expect_false(contains_runtime_object(trm))

roundtrip <- unserialize(serialize(trm, NULL))
expect_identical(roundtrip, trm)

attention_input <- rllm_input(
    "attention_input", c(feature = 8L, sequence = "n_token")
)
attention_program <- rllm_attention(
    attention_input,
    query = rllm_parameter("attention.q", c(8L, 8L)),
    key = rllm_parameter("attention.k", c(8L, 4L)),
    value = rllm_parameter("attention.v", c(8L, 4L)),
    output = rllm_parameter("attention.output", c(8L, 8L)),
    heads = 2L,
    kv_heads = 1L,
    state = list(op = "kv", width = 4L)
) |>
    rllm_program("attention_vocabulary")
attention_node <- Filter(function(node) {
    node$op == "attention"
}, attention_program$nodes)[[1L]]
expect_equal(attention_node$attributes$n_head, 2L)
expect_equal(attention_node$attributes$n_head_kv, 1L)
expect_equal(attention_node$attributes$head_dim, 4L)
expect_equal(attention_node$attributes$state, list(op = "kv", width = 4L))
expect_false(any(c("heads", "kv_heads", "specification") %in%
                 names(attention_node$attributes)))

leaky <- 1
attr(leaky, "builder") <- new.env(parent = emptyenv())
expect_error(
    rllm_op(input_embeddings, "opaque_attribute", payload = leaky),
    "data only"
)

foreign <- rllm_input("foreign", c(feature = "hidden"))
pair_module <- rllm_module("pair", function(x) x[[1L]])
expect_error(pair_module(list(input_embeddings, foreign)), "different programs")

message("R module and pipe program tests completed")
