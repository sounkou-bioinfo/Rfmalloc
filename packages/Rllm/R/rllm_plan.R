#' Inspect the normalized checkpoint description of a GGUF model
#'
#' The executable [rllm_program()] is the source of truth. `rllm_plan()`
#' derives a compact layer-oriented inspection view from the program's
#' validated GGML lowering. Constructing or loading a model does not retain
#' this second representation.
#'
#' @param x An `rllm_model` or a path to a GGUF file.
#' @return An inspectable object of class `rllm_plan`.
#' @export
rllm_plan <- function(x) {
    if (inherits(x, "rllm_model")) {
        return(.rllm_plan_from_bound(x$execution))
    }
    if (!is.character(x) || length(x) != 1L || is.na(x)) {
        stop("x must be an rllm_model or one GGUF path")
    }
    definition <- .rllm_program_from_gguf(
        Rgguf::gguf_metadata(x),
        Rgguf::gguf_tensors(x)
    )
    .rllm_plan_from_program(definition$program, definition$symbols)
}

.rllm_plan_from_program <- function(program, symbols) {
    .rllm_plan_view(program, .rllm_lower_program(program, symbols))
}

.rllm_plan_from_bound <- function(bound) {
    .rllm_plan_view(bound$program, bound$lowering)
}

.rllm_plan_view <- function(program, lowering) {
    tensors <- lapply(program$parameters, function(parameter) {
        structure(
            list(
                name = parameter$name,
                role = parameter$role,
                shape = as.integer(parameter$shape)
            ),
            class = "rllm_tensor_binding"
        )
    })
    structure(
        list(
            architecture = program$name,
            symbols = lowering$symbols,
            tensors = tensors,
            input = lowering$input,
            layers = lowering$layers,
            output = lowering$output,
            program = program
        ),
        class = c("rllm_plan", "list")
    )
}

#' @export
print.rllm_plan <- function(x, ...) {
    operators <- vapply(x$layers, function(layer) layer$operator$op,
                        character(1))
    ffns <- vapply(x$layers, function(layer) layer$feed_forward$op,
                   character(1))
    cat(sprintf(
        "<rllm_plan %s: %d layers, %d tensors; operators %s; feed-forward %s>\n",
        x$architecture, length(x$layers), length(x$tensors),
        .rllm_plan_counts(operators), .rllm_plan_counts(ffns)
    ))
    invisible(x)
}

.rllm_plan_counts <- function(x) {
    tab <- sort(table(x), decreasing = TRUE)
    paste0(names(tab), "=", as.integer(tab), collapse = "/")
}

.rllm_missing <- new.env(parent = emptyenv())

.rllm_metadata <- function(metadata, architecture, key,
                           default = .rllm_missing) {
    name <- paste0(architecture, ".", key)
    value <- metadata[[name]]
    if (is.null(value)) {
        if (identical(default, .rllm_missing)) {
            stop("missing GGUF hyperparameter '", name, "'")
        }
        return(default)
    }
    value
}

.rllm_integer <- function(x, name) {
    if (!is.numeric(x) || anyNA(x) || any(!is.finite(x)) ||
        any(x < 0) || any(x != floor(x)) || any(x > .Machine$integer.max)) {
        stop("GGUF hyperparameter '", name,
             "' must contain non-negative integers")
    }
    as.integer(x)
}

.rllm_positive <- function(x, name) {
    value <- .rllm_integer(x, name)
    if (any(value < 1L)) {
        stop("GGUF hyperparameter '", name, "' must be positive")
    }
    value
}

.rllm_validate_directory <- function(directory) {
    if (!is.data.frame(directory) ||
        !all(c("name", "dims") %in% names(directory))) {
        stop("GGUF tensor directory must contain name and dims columns")
    }
    if (anyDuplicated(directory$name)) {
        stop("GGUF tensor directory contains duplicate names")
    }
    invisible(directory)
}

.rllm_validate_program <- function(program, directory) {
    .rllm_validate_directory(directory)
    for (parameter in program$parameters) {
        at <- match(parameter$name, directory$name)
        role <- parameter$role
        if (is.null(role)) role <- "parameter"
        if (is.na(at)) {
            stop("missing tensor '", parameter$name, "' for role '", role, "'")
        }
        actual <- as.integer(directory$dims[[at]])
        expected <- as.integer(parameter$shape)
        if (!identical(actual, expected)) {
            stop(
                "tensor '", parameter$name, "' for role '", role,
                "' has shape [", paste(actual, collapse = ", "),
                "], expected [", paste(expected, collapse = ", "), "]"
            )
        }
    }
    program
}
