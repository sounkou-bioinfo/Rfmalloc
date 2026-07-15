#' Build a typed model program with ordinary R functions and pipes
#'
#' These functions form the R-facing architecture language. A program is
#' traced from ordinary R calls, including functions returned by
#' `rllm_module()`, residual branches and structured loops. Calling
#' `rllm_program()` freezes the trace as data only: nodes, tensor parameters,
#' shapes, module paths and named outputs. The resulting object contains no R
#' closures, environments or backend pointers.
#'
#' This is an architecture language, not a second tensor runtime. Operators
#' describe semantics; a backend may lower the operators it implements and
#' must reject the rest explicitly.
#'
#' @param name A non-empty input, parameter, module, loop or program name.
#' @param shape An atomic vector describing axes. Dimensions may be positive
#'   numbers or symbolic character strings.
#' @param dtype Storage or value type.
#' @param role Optional semantic role for a parameter.
#' @param forward An ordinary R function whose first argument is a traced
#'   value. Its body may use base R's `|>` pipe.
#' @param x A traced `rllm_value`, a named list of traced values for a
#'   multi-input operator, a model plan, a loaded model, or a GGUF path.
#' @param op A non-empty semantic operator name.
#' @param ... Named, data-only operator attributes, or arguments passed to a
#'   module's `forward` function.
#' @param output_shape Optional result shape. The input shape is retained when
#'   omitted.
#' @param output_dtype Optional result type. The input type is retained when
#'   omitted.
#'
#' @return `rllm_input()` and operators return a traced `rllm_value`.
#'   `rllm_parameter()` returns a data-only tensor reference,
#'   `rllm_module()` returns a callable R function, and `rllm_program()`
#'   returns a data-only `rllm_program`.
#' @export
rllm_input <- function(name, shape, dtype = "f32") {
    builder <- .rllm_new_builder(name)
    .rllm_add_input(builder, name, shape, dtype)
}

#' @rdname rllm_input
#' @export
rllm_parameter <- function(name, shape, dtype = NULL, role = NULL) {
    .rllm_name(name, "parameter name")
    shape <- .rllm_program_shape(shape)
    if (!is.null(dtype)) .rllm_name(dtype, "parameter dtype")
    if (!is.null(role)) .rllm_name(role, "parameter role")
    structure(list(name = name, shape = shape, dtype = dtype, role = role),
              class = c("rllm_parameter", "list"))
}

#' @rdname rllm_input
#' @export
rllm_module <- function(name, forward) {
    .rllm_name(name, "module name")
    if (!is.function(forward)) stop("forward must be an R function")
    module <- function(x, ...) {
        values <- .rllm_values(x)
        builder <- values[[1L]]$.builder
        .rllm_assert_builder(x, builder)
        old <- builder$module
        builder$module <- c(old, name)
        on.exit(builder$module <- old, add = TRUE)
        out <- forward(x, ...)
        .rllm_assert_builder(out, builder)
        out
    }
    attr(module, "name") <- name
    class(module) <- c("rllm_module", "function")
    module
}

#' @rdname rllm_input
#' @export
rllm_op <- function(x, op, ..., output_shape = NULL,
                    output_dtype = NULL) {
    .rllm_name(op, "operator name")
    values <- .rllm_values(x)
    value <- values[[1L]]
    .rllm_assert_builder(x, value$.builder)
    attrs <- list(...)
    .rllm_named(attrs, "operator attributes")
    if (is.null(output_shape)) output_shape <- value$shape
    if (is.null(output_dtype)) output_dtype <- value$dtype
    .rllm_append(value$.builder, op, values, attrs,
                  output_shape, output_dtype)
}

#' Typed linear, normalization, attention and pooling operators
#'
#' These are convenient typed constructors over [rllm_op()]. They keep the
#' surface close to an R module's `forward` method while recording explicit
#' parameter identities and semantic attributes in the frozen program.
#'
#' @param x A traced `rllm_value`.
#' @param weight,bias Tensor references from [rllm_parameter()]. `bias` is
#'   optional.
#' @param kind Operator kind.
#' @param eps Normalization epsilon.
#' @param query,key,value,output Projection parameters for attention.
#' @param heads,kv_heads Query and key/value head counts.
#' @param query_norm,key_norm Optional per-head normalization parameters.
#' @param rope,mask,scale Data-only attention specifications.
#'
#' @return A traced `rllm_value`.
#' @export
rllm_linear <- function(x, weight, bias = NULL) {
    value <- .rllm_one_value(x)
    .rllm_expect_parameter(weight, "weight", 2L)
    if (!is.null(bias)) .rllm_expect_parameter(bias, "bias", 1L)
    if (length(value$shape) < 1L ||
        !.rllm_same_dimension(value$shape[[1L]], weight$shape[[1L]])) {
        stop("linear input and weight dimensions do not agree")
    }
    if (!is.null(bias) &&
        !.rllm_same_dimension(bias$shape[[1L]], weight$shape[[2L]])) {
        stop("linear bias and output dimensions do not agree")
    }
    shape <- value$shape
    shape[[1L]] <- weight$shape[[2L]]
    rllm_op(value, "linear", weight = weight, bias = bias,
            output_shape = shape)
}

#' @rdname rllm_linear
#' @export
rllm_norm <- function(x, weight, kind = c("rms", "layer"), eps = 1e-5,
                      bias = NULL) {
    value <- .rllm_one_value(x)
    kind <- match.arg(kind)
    .rllm_expect_parameter(weight, "weight", 1L)
    if (!is.null(bias)) .rllm_expect_parameter(bias, "bias", 1L)
    if (length(value$shape) < 1L ||
        !.rllm_same_dimension(value$shape[[1L]], weight$shape[[1L]]) ||
        (!is.null(bias) &&
         !.rllm_same_dimension(weight$shape[[1L]], bias$shape[[1L]]))) {
        stop("normalization parameters do not match the feature dimension")
    }
    if (!is.numeric(eps) || length(eps) != 1L || !is.finite(eps) || eps <= 0) {
        stop("eps must be one positive finite number")
    }
    rllm_op(value, paste0(kind, "_norm"), weight = weight, bias = bias,
            eps = as.numeric(eps))
}

#' @rdname rllm_linear
#' @export
rllm_attention <- function(x, query, key, value, output, heads,
                           kv_heads = heads, query_norm = NULL,
                           key_norm = NULL, rope = NULL,
                           mask = list(type = "causal"), scale = NULL) {
    input <- .rllm_one_value(x)
    for (item in list(query = query, key = key, value = value,
                      output = output)) {
        .rllm_expect_parameter(item, "attention projection", 2L)
    }
    for (item in list(query_norm = query_norm, key_norm = key_norm)) {
        if (!is.null(item)) .rllm_expect_parameter(item, "head norm", 1L)
    }
    heads <- .rllm_count(heads, "heads")
    kv_heads <- .rllm_count(kv_heads, "kv_heads")
    if (heads %% kv_heads != 0L) stop("heads must be divisible by kv_heads")
    if (length(input$shape) < 1L ||
        !.rllm_same_dimension(input$shape[[1L]], query$shape[[1L]]) ||
        !.rllm_same_dimension(input$shape[[1L]], key$shape[[1L]]) ||
        !.rllm_same_dimension(input$shape[[1L]], value$shape[[1L]]) ||
        !.rllm_same_dimension(input$shape[[1L]], output$shape[[2L]]) ||
        !.rllm_same_dimension(query$shape[[2L]], output$shape[[1L]]) ||
        !.rllm_same_dimension(key$shape[[2L]], value$shape[[2L]])) {
        stop("attention projections do not preserve the feature dimension")
    }
    q_width <- suppressWarnings(as.numeric(query$shape[[2L]]))
    kv_width <- suppressWarnings(as.numeric(key$shape[[2L]]))
    if (!is.na(q_width) && !is.na(kv_width) &&
        (q_width %% heads != 0 || kv_width %% kv_heads != 0 ||
         q_width / heads != kv_width / kv_heads)) {
        stop("attention projections and head counts have inconsistent widths")
    }
    head_width <- if (!is.na(q_width)) q_width / heads else NULL
    if (!is.null(head_width) &&
        ((!is.null(query_norm) &&
          !.rllm_same_dimension(query_norm$shape[[1L]], head_width)) ||
         (!is.null(key_norm) &&
          !.rllm_same_dimension(key_norm$shape[[1L]], head_width)))) {
        stop("attention head normalization has the wrong width")
    }
    rllm_op(input, "attention", query = query, key = key, value = value,
            output = output, heads = heads, kv_heads = kv_heads,
            query_norm = query_norm, key_norm = key_norm,
            rope = rope, mask = mask, scale = scale)
}

#' @rdname rllm_linear
#' @export
rllm_pool <- function(x, kind = c("mean", "cls", "none")) {
    value <- .rllm_one_value(x)
    kind <- match.arg(kind)
    shape <- if (kind == "none") value$shape else value$shape[[1L]]
    axes <- names(value$shape)
    names(shape) <- if (kind == "none") axes else if (length(axes)) axes[[1L]]
    rllm_op(value, "pool", kind = kind, output_shape = shape)
}

#' Trace a residual branch
#'
#' `branch` is evaluated immediately with `x`. Its nodes are recorded, then an
#' explicit add node joins the original value and the branch result.
#'
#' @param x A traced `rllm_value`.
#' @param branch A function of one traced value.
#'
#' @return A traced `rllm_value`.
#' @export
rllm_residual <- function(x, branch) {
    value <- .rllm_one_value(x)
    if (!is.function(branch)) stop("branch must be an R function")
    out <- .rllm_one_value(branch(value))
    if (!identical(out$.builder, value$.builder)) {
        stop("residual branch belongs to a different program")
    }
    if (!.rllm_same_shape(value$shape, out$shape)) {
        stop("residual branch must preserve shape")
    }
    .rllm_append(value$.builder, "add", list(value, out), list(),
                  value$shape, value$dtype)
}

#' Name an intermediate program output
#'
#' @param x A traced `rllm_value`.
#' @param name A unique output name.
#'
#' @return `x`, invisibly annotated in its builder so a pipe may continue.
#' @export
rllm_tap <- function(x, name) {
    value <- .rllm_one_value(x)
    .rllm_name(name, "output name")
    if (!is.null(value$.builder$outputs[[name]])) {
        stop("program output '", name, "' already exists")
    }
    value$.builder$outputs[[name]] <- value$id
    value
}

#' Trace a structured, shared recurrence
#'
#' Unlike an R `for` loop, `rllm_loop()` does not unroll its body. The body is
#' traced once into a nested data-only program and retained as a loop node.
#' A single value supports pipe syntax. A named list supports multiple carried
#' values, as required by recurrent answer and latent states.
#'
#' @param x A traced value or named list of values from one program.
#' @param times A positive integer or one symbolic count.
#' @param body A function returning the same value structure as `x`.
#' @param name Loop name.
#'
#' @return A traced value, or a named list of traced values when `x` is a list.
#' @export
rllm_loop <- function(x, times, body, name = "loop") {
    .rllm_name(name, "loop name")
    if (!(is.numeric(times) && length(times) == 1L && is.finite(times) &&
          times >= 1 && times == floor(times)) &&
        !(is.character(times) && length(times) == 1L && !is.na(times) &&
          nzchar(times))) {
        stop("times must be a positive integer or one symbolic name")
    }
    if (!is.function(body)) stop("body must be an R function")
    many <- is.list(x) && !inherits(x, "rllm_value")
    state <- if (many) x else list(state = .rllm_one_value(x))
    if (many && (is.null(names(state)) || any(!nzchar(names(state))) ||
                 anyDuplicated(names(state)))) {
        stop("multiple loop values must have unique names")
    }
    values <- .rllm_values(state)
    builder <- values[[1L]]$.builder
    .rllm_assert_builder(state, builder)

    child <- .rllm_new_builder(name)
    placeholders <- Map(function(value, carry) {
        .rllm_add_input(child, carry, value$shape, value$dtype)
    }, values, names(state))
    names(placeholders) <- names(state)
    traced <- if (many) body(placeholders) else body(placeholders[[1L]])
    result <- if (many) traced else list(state = .rllm_one_value(traced))
    if (many && (!is.list(result) || !identical(names(result), names(state)))) {
        stop("loop body must return the same named carries")
    }
    .rllm_assert_builder(result, child)
    body_program <- .rllm_snapshot(result, name)

    tuple <- .rllm_append(builder, "loop", values,
                          list(name = name, times = times,
                               carries = names(state), body = body_program),
                          if (many) NULL else result[[1L]]$shape,
                          if (many) "tuple" else result[[1L]]$dtype)
    if (!many) return(tuple)
    out <- Map(function(value, carry, index) {
        .rllm_append(builder, "loop_result", list(tuple),
                      list(carry = carry, index = index),
                      value$shape, value$dtype)
    }, result, names(result), seq_along(result) - 1L)
    names(out) <- names(result)
    out
}

#' @rdname rllm_input
#' @export
rllm_program <- function(x, name = NULL) {
    if (inherits(x, "rllm_model")) return(x$plan$program)
    if (inherits(x, "rllm_plan")) return(x$program)
    if (is.character(x) && length(x) == 1L && !is.na(x)) {
        return(rllm_plan(x)$program)
    }
    .rllm_snapshot(x, name)
}

#' @export
print.rllm_program <- function(x, ...) {
    operators <- vapply(x$nodes, `[[`, character(1), "op")
    cat(sprintf("<rllm_program %s: %d nodes, %d parameters, %d outputs; %s>\n",
                x$name, length(x$nodes), length(x$parameters),
                length(x$outputs), .rllm_plan_counts(operators)))
    invisible(x)
}

.rllm_new_builder <- function(name) {
    builder <- new.env(parent = emptyenv())
    builder$name <- name
    builder$nodes <- list()
    builder$parameters <- list()
    builder$outputs <- list()
    builder$module <- character()
    builder
}

.rllm_add_input <- function(builder, name, shape, dtype) {
    .rllm_name(name, "input name")
    .rllm_name(dtype, "input dtype")
    .rllm_append(builder, "input", list(), list(name = name), shape, dtype)
}

.rllm_append <- function(builder, op, inputs, attrs, shape, dtype) {
    shape <- .rllm_program_shape(shape, allow_null = TRUE)
    .rllm_name(dtype, "value dtype")
    .rllm_assert_data(attrs, "operator attributes")
    .rllm_register_parameters(builder, attrs)
    id <- paste0("n", length(builder$nodes) + 1L)
    node <- list(
        id = id,
        op = op,
        inputs = vapply(inputs, `[[`, character(1), "id"),
        shape = shape,
        dtype = dtype,
        attributes = attrs,
        module = builder$module
    )
    builder$nodes[[length(builder$nodes) + 1L]] <- node
    structure(list(.builder = builder, id = id, shape = shape, dtype = dtype),
              class = "rllm_value")
}

.rllm_snapshot <- function(x, name = NULL) {
    values <- .rllm_values(x)
    builder <- values[[1L]]$.builder
    .rllm_assert_builder(x, builder)
    outputs <- builder$outputs
    value_names <- names(x)
    if (is.null(value_names) || any(!nzchar(value_names))) {
        value_names <- if (length(values) == 1L) "output" else
            paste0("output_", seq_along(values))
    }
    for (i in seq_along(values)) {
        output_name <- value_names[[i]]
        existing <- outputs[[output_name]]
        if (is.null(existing)) {
            outputs[[output_name]] <- values[[i]]$id
        } else if (!identical(existing, values[[i]]$id)) {
            stop("final output name '", output_name,
                 "' already names a different tap")
        }
    }
    if (is.null(name)) name <- builder$name
    .rllm_name(name, "program name")
    structure(list(
        name = name,
        nodes = builder$nodes,
        parameters = builder$parameters,
        outputs = outputs
    ), class = c("rllm_program", "list"))
}

.rllm_values <- function(x) {
    if (inherits(x, "rllm_value")) return(list(x))
    if (!is.list(x) || !length(x) ||
        !all(vapply(x, inherits, logical(1), "rllm_value"))) {
        stop("expected a traced rllm_value or a non-empty list of them")
    }
    x
}

.rllm_one_value <- function(x) {
    values <- .rllm_values(x)
    if (length(values) != 1L) stop("operator expects one traced value")
    values[[1L]]
}

.rllm_assert_builder <- function(x, builder) {
    values <- .rllm_values(x)
    if (any(!vapply(values, function(value) identical(value$.builder, builder),
                    logical(1)))) {
        stop("traced values belong to different programs")
    }
    invisible(x)
}

.rllm_name <- function(x, what) {
    if (!is.character(x) || length(x) != 1L || is.na(x) || !nzchar(x)) {
        stop(what, " must be one non-empty string")
    }
    invisible(x)
}

.rllm_named <- function(x, what) {
    if (length(x) && (is.null(names(x)) || any(!nzchar(names(x))) ||
                      anyDuplicated(names(x)))) {
        stop(what, " must have unique non-empty names")
    }
    invisible(x)
}

.rllm_program_shape <- function(shape, allow_null = FALSE) {
    if (is.null(shape) && allow_null) return(NULL)
    if (!is.atomic(shape) || !length(shape) ||
        !(is.numeric(shape) || is.character(shape))) {
        stop("shape must be a non-empty numeric or character vector")
    }
    if (is.numeric(shape) &&
        (anyNA(shape) || any(!is.finite(shape)) || any(shape < 1) ||
         any(shape != floor(shape)))) {
        stop("numeric shape dimensions must be positive integers")
    }
    if (is.character(shape) && (anyNA(shape) || any(!nzchar(shape)))) {
        stop("symbolic shape dimensions must be non-empty")
    }
    shape
}

.rllm_same_dimension <- function(x, y) identical(as.character(x), as.character(y))

.rllm_same_shape <- function(x, y) {
    length(x) == length(y) &&
        all(unlist(Map(.rllm_same_dimension, as.list(x), as.list(y)),
                   use.names = FALSE))
}

.rllm_count <- function(x, what) {
    if (!is.numeric(x) || length(x) != 1L || is.na(x) || !is.finite(x) ||
        x < 1 || x != floor(x) || x > .Machine$integer.max) {
        stop(what, " must be one positive integer")
    }
    as.integer(x)
}

.rllm_expect_parameter <- function(x, what, rank = NULL) {
    if (!inherits(x, "rllm_parameter")) {
        stop(what, " must come from rllm_parameter()")
    }
    if (!is.null(rank) && length(x$shape) != rank) {
        stop(what, " must have rank ", rank)
    }
    invisible(x)
}

.rllm_assert_data <- function(x, what) {
    bad <- typeof(x) %in% c(
        "closure", "environment", "externalptr", "weakref", "promise",
        "language", "symbol"
    )
    if (bad) stop(what, " must contain data only")
    if (is.list(x)) {
        for (item in x) .rllm_assert_data(item, what)
    }
    attrs <- attributes(x)
    if (!is.null(attrs)) {
        for (item in attrs) .rllm_assert_data(item, what)
    }
    invisible(x)
}

.rllm_register_parameters <- function(builder, x) {
    if (inherits(x, "rllm_parameter")) {
        old <- builder$parameters[[x$name]]
        if (!is.null(old) && !identical(unclass(old), unclass(x))) {
            stop("parameter '", x$name, "' has conflicting declarations")
        }
        builder$parameters[[x$name]] <- x
        return(invisible(NULL))
    }
    if (is.list(x)) {
        for (item in x) .rllm_register_parameters(builder, item)
    }
    invisible(NULL)
}

.rllm_plan_parameter <- function(plan, name) {
    binding <- plan$tensors[[name]]
    if (is.null(binding)) stop("plan refers to unknown tensor '", name, "'")
    rllm_parameter(binding$name, binding$shape, role = binding$role)
}

.rllm_bind_plan_parameters <- function(x, plan) {
    if (is.character(x) && length(x) == 1L && !is.null(plan$tensors[[x]])) {
        return(.rllm_plan_parameter(plan, x))
    }
    if (is.list(x)) return(lapply(x, .rllm_bind_plan_parameters, plan = plan))
    x
}

.rllm_program_from_plan <- function(plan) {
    n_embd <- plan$symbols$n_embd
    n_token <- "n_token"
    tokens <- rllm_input("tokens", c(sequence = n_token), "i32")
    x <- rllm_op(
        tokens, "embedding",
        weight = .rllm_plan_parameter(plan, plan$input$weight),
        scale = plan$input$scale,
        output_shape = c(feature = as.character(n_embd), sequence = n_token),
        output_dtype = "f32"
    )
    eps <- plan$symbols$rms_eps

    for (layer in plan$layers) {
        block <- rllm_module(paste0("block.", layer$index), function(x) {
            x <- rllm_residual(x, function(branch) {
                branch <- rllm_norm(
                    branch,
                    .rllm_plan_parameter(plan, layer$operator_norm),
                    eps = eps
                )
                branch <- rllm_op(
                    branch, layer$operator$op,
                    specification = .rllm_bind_plan_parameters(
                        layer$operator, plan
                    ),
                    state = layer$state
                )
                if (!is.null(layer$operator_post_norm)) {
                    branch <- rllm_norm(
                        branch,
                        .rllm_plan_parameter(plan, layer$operator_post_norm),
                        eps = eps
                    )
                }
                branch
            })
            rllm_residual(x, function(branch) {
                branch <- rllm_norm(
                    branch,
                    .rllm_plan_parameter(plan, layer$ffn_norm),
                    eps = eps
                )
                branch <- rllm_op(
                    branch, layer$feed_forward$op,
                    specification = .rllm_bind_plan_parameters(
                        layer$feed_forward, plan
                    )
                )
                if (!is.null(layer$feed_forward_post_norm)) {
                    branch <- rllm_norm(
                        branch,
                        .rllm_plan_parameter(
                            plan, layer$feed_forward_post_norm
                        ),
                        eps = eps
                    )
                }
                branch
            })
        })
        x <- block(x)
    }

    x <- rllm_norm(x, .rllm_plan_parameter(plan, plan$output$norm), eps = eps)
    if (plan$output$op == "projection") {
        x <- rllm_linear(x, .rllm_plan_parameter(plan, plan$output$weight))
    } else {
        x <- rllm_pool(x, plan$output$pooling)
        if (!is.null(plan$output$projection_1)) {
            x <- rllm_linear(
                x, .rllm_plan_parameter(plan, plan$output$projection_1)
            )
            x <- rllm_linear(
                x, .rllm_plan_parameter(plan, plan$output$projection_2)
            )
        }
    }
    program <- rllm_program(x, plan$architecture)
    missing <- setdiff(names(plan$tensors), names(program$parameters))
    if (length(missing)) {
        stop("semantic program did not bind tensors: ",
             paste(missing, collapse = ", "))
    }
    program
}
