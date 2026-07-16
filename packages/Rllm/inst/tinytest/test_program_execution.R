library(tinytest)
library(Rllm)

# This is the evaluation-time TRM recurrence from Samsung's implementation:
# each improvement step contains H cycles, each H cycle contains L updates of
# z_L from z_L + z_H + x, then one update of z_H from z_H + z_L. One reasoning
# module and one weight are shared by every nested iteration.
hidden <- 3L
sequence <- 2L
weight <- rllm_parameter("trm.reasoning.weight", c(hidden, hidden))
h_init <- rllm_parameter("trm.H_init", hidden)
l_init <- rllm_parameter("trm.L_init", hidden)

reasoning <- rllm_module("trm.reasoning", function(pair) {
    pair |>
        rllm_op("add") |>
        rllm_linear(weight) |>
        rllm_op("tanh")
})

embedding <- rllm_input(
    "input_embeddings", c(feature = hidden, sequence = sequence)
)
state <- list(
    z_H = embedding |> rllm_op("broadcast_initial", value = h_init),
    z_L = embedding |> rllm_op("broadcast_initial", value = l_init),
    input = embedding
)
state <- rllm_loop(state, "halt_max_steps", function(improvement) {
    rllm_loop(improvement, "H_cycles", function(high) {
        low <- rllm_loop(high, "L_cycles", function(low) {
            list(
                z_H = low$z_H,
                z_L = reasoning(list(
                    hidden = low$z_L,
                    high = low$z_H,
                    input = low$input
                )),
                input = low$input
            )
        }, name = "low_cycle")
        list(
            z_H = reasoning(list(hidden = low$z_H, low = low$z_L)),
            z_L = low$z_L,
            input = low$input
        )
    }, name = "high_cycle")
}, name = "improvement_step")
program <- rllm_program(list(z_H = state$z_H, z_L = state$z_L),
                        "trm_recurrence")

input <- matrix(c(0.2, -0.1, 0.3, 0.4, 0.1, -0.2), nrow = hidden)
parameters <- list(
    trm.reasoning.weight = matrix(
        c(0.2, -0.1, 0.3, 0.4, 0.2, -0.2, -0.3, 0.1, 0.5),
        nrow = hidden
    ),
    trm.H_init = c(0.1, 0.2, -0.1),
    trm.L_init = c(-0.2, 0.3, 0.1)
)
counts <- c(halt_max_steps = 2L, H_cycles = 3L, L_cycles = 2L)

actual <- rllm_execute(
    program,
    inputs = list(input_embeddings = input),
    parameters = parameters,
    counts = counts
)

reason <- function(...) {
    value <- Reduce(`+`, list(...))
    tanh(crossprod(parameters$trm.reasoning.weight, value))
}
z_H <- matrix(parameters$trm.H_init, hidden, sequence)
z_L <- matrix(parameters$trm.L_init, hidden, sequence)
for (improvement in seq_len(counts[["halt_max_steps"]])) {
    for (high in seq_len(counts[["H_cycles"]])) {
        for (low in seq_len(counts[["L_cycles"]])) {
            z_L <- reason(z_L, z_H, input)
        }
        z_H <- reason(z_H, z_L)
    }
}

expect_equal(actual$z_H, z_H, tolerance = 1e-15)
expect_equal(actual$z_L, z_L, tolerance = 1e-15)
expect_equal(length(Filter(function(node) node$op == "loop", program$nodes)),
             1L)

custom <- rllm_input("x", c(feature = 2L)) |>
    rllm_op("affine_shift", amount = 3) |>
    rllm_program("custom_lowering")
seen <- NULL
custom_result <- rllm_execute(
    custom,
    inputs = list(x = c(1, 2)),
    operators = list(affine_shift = function(inputs, attributes, parameters,
                                              context) {
        seen <<- context
        inputs[[1L]] + attributes$amount
    })
)
expect_equal(custom_result$output, c(4, 5))
expect_equal(seen$node$op, "affine_shift")
expect_equal(seen$inputs$x, c(1, 2))

multi_inputs <- rllm_inputs(
    token = c(sequence = 2L),
    mask = c(sequence = 2L),
    .dtype = c(token = "i32", mask = "logical"),
    .name = "multiple_inputs"
)
multi_result <- rllm_op(
    multi_inputs, "split_result",
    outputs = list(
        kept = list(shape = c(sequence = 2L), dtype = "i32"),
        count = list(shape = c(value = 1L), dtype = "i32")
    )
)
multi_program <- rllm_program(multi_result, "multiple_results")
multi_actual <- rllm_execute(
    multi_program,
    inputs = list(token = c(4L, 7L), mask = c(TRUE, FALSE)),
    operators = list(split_result = function(inputs, attributes, parameters,
                                              context) {
        list(kept = inputs$token[inputs$mask], count = sum(inputs$mask))
    })
)
expect_equal(multi_actual$kept, 4L)
expect_equal(multi_actual$count, 1L)

expect_error(
    rllm_execute(program, list(input_embeddings = input), parameters,
                 counts[-1L]),
    "needs count 'halt_max_steps'"
)
expect_error(
    rllm_execute(custom, list(x = c(1, 2))),
    "no lowering registered"
)
bad_inputs <- list(c(1, 2))
names(bad_inputs) <- NA_character_
expect_error(rllm_execute(custom, bad_inputs), "unique non-empty names")

message("R program execution tests completed")
