.rllm_lower_parameter <- function(x, what) {
  if (!inherits(x, "rllm_parameter")) {
    stop(what, " must be an rllm_parameter")
  }
  x$name
}

.rllm_lower_attributes <- function(x) {
  if (inherits(x, "rllm_parameter")) {
    return(x$name)
  }
  if (!is.list(x)) {
    return(x)
  }
  lapply(x, .rllm_lower_attributes)
}

.rllm_lower_program <- function(program, symbols) {
  if (!inherits(program, "rllm_program")) {
    stop("program must come from rllm_program()")
  }
  if (!is.list(symbols) || is.null(names(symbols))) {
    stop("program symbols must be a named list")
  }
  nodes <- program$nodes
  if (!is.list(nodes) || !length(nodes)) {
    stop("program has no nodes")
  }
  inputs <- Filter(function(node) identical(node$op, "input"), nodes)
  if (length(inputs) != 1L) {
    stop(
      "native GGML lowering does not implement the program's ",
      length(inputs), "-input grammar"
    )
  }
  ids <- vapply(nodes, `[[`, character(1), "id")
  if (anyDuplicated(ids)) {
    stop("program node ids must be unique")
  }

  cursor <- 1L
  peek <- function(offset = 0L) {
    at <- cursor + offset
    if (at > length(nodes)) {
      return(NULL)
    }
    nodes[[at]]
  }
  take <- function(op, what) {
    node <- peek()
    if (is.null(node) || !identical(node$op, op)) {
      found <- if (is.null(node)) {
        "end of program"
      } else {
        paste0("operator '", node$op, "'")
      }
      stop(what, " must be ", op, ", found ", found)
    }
    cursor <<- cursor + 1L
    node
  }
  expect_inputs <- function(node, expected, what) {
    actual <- unname(node$inputs)
    if (!identical(actual, expected)) {
      stop(what, " has an invalid dataflow edge")
    }
    invisible(node)
  }
  norm <- function(node, input, what) {
    expect_inputs(node, input, what)
    if (!is.null(node$attributes$bias)) {
      stop(what, " uses a bias unsupported by the GGML lowering")
    }
    eps <- node$attributes$eps
    if (!is.numeric(eps) || length(eps) != 1L || !is.finite(eps) || eps <= 0) {
      stop(what, " has an invalid epsilon")
    }
    list(
      weight = .rllm_lower_parameter(
        node$attributes$weight,
        paste0(what, " weight")
      ),
      eps = as.numeric(eps)
    )
  }
  post_norm <- function(input, what) {
    node <- peek()
    after <- peek(1L)
    if (
      is.null(node) ||
        is.null(after) ||
        !identical(node$op, "rms_norm") ||
        !identical(unname(node$inputs), input) ||
        !identical(after$op, "add")
    ) {
      return(list(id = input, weight = NULL, eps = NULL))
    }
    cursor <<- cursor + 1L
    lowered <- norm(node, input, what)
    list(id = node$id, weight = lowered$weight, eps = lowered$eps)
  }
  integer_dimension <- function(x, what) {
    if (
      is.character(x) && length(x) == 1L && !is.na(x) && grepl("^[0-9]+$", x)
    ) {
      x <- as.numeric(x)
    }
    if (
      !is.numeric(x) ||
        length(x) != 1L ||
        is.na(x) ||
        !is.finite(x) ||
        x < 1 ||
        x != floor(x) ||
        x > .Machine$integer.max
    ) {
      stop(what, " must be one resolved positive dimension")
    }
    as.integer(x)
  }

  token <- take("input", "program entry")
  if (
    !identical(token$attributes$name, "tokens") ||
      !identical(token$dtype, "i32")
  ) {
    stop("native program input must be i32 tokens")
  }
  embedding <- take("embedding", "program input lowering")
  expect_inputs(embedding, token$id, "embedding")
  embedding_weight <- embedding$attributes$weight
  embedding_name <- .rllm_lower_parameter(
    embedding_weight,
    "embedding weight"
  )
  if (length(embedding_weight$shape) != 2L) {
    stop("embedding weight must have rank two")
  }
  n_embd <- integer_dimension(
    embedding_weight$shape[[1L]],
    "embedding feature dimension"
  )
  n_vocab <- integer_dimension(
    embedding_weight$shape[[2L]],
    "embedding vocabulary dimension"
  )
  input <- list(
    op = "embedding",
    weight = embedding_name,
    scale = as.numeric(embedding$attributes$scale)
  )
  if (length(input$scale) != 1L || !is.finite(input$scale)) {
    stop("embedding scale must be one finite number")
  }

  operator_ops <- c(
    "attention",
    "gated_attention",
    "gated_delta_net",
    "shortconv"
  )
  feed_forward_ops <- c("swiglu", "geglu", "moe_swiglu")
  layers <- list()
  eps <- numeric()
  current <- embedding$id

  repeat {
    operator_norm_node <- peek()
    operator_node <- peek(1L)
    if (
      is.null(operator_norm_node) ||
        is.null(operator_node) ||
        !identical(operator_norm_node$op, "rms_norm") ||
        !(operator_node$op %in% operator_ops)
    ) {
      break
    }

    cursor <- cursor + 2L
    layer_index <- length(layers)
    prefix <- paste0("layer ", layer_index)
    operator_norm <- norm(
      operator_norm_node,
      current,
      paste0(prefix, " operator norm")
    )
    expect_inputs(
      operator_node,
      operator_norm_node$id,
      paste0(prefix, " operator")
    )
    attributes <- operator_node$attributes
    state <- attributes$state
    attributes$state <- NULL
    operator <- c(list(op = operator_node$op), attributes)
    if (!is.list(state) || !is.character(state$op) || length(state$op) != 1L) {
      stop(prefix, " has no valid state declaration")
    }
    operator_post <- post_norm(
      operator_node$id,
      paste0(prefix, " operator post-norm")
    )
    residual <- take("add", paste0(prefix, " operator residual"))
    expect_inputs(
      residual,
      c(current, operator_post$id),
      paste0(prefix, " operator residual")
    )

    ffn_norm_node <- take(
      "rms_norm",
      paste0(prefix, " feed-forward norm")
    )
    ffn_norm <- norm(
      ffn_norm_node,
      residual$id,
      paste0(prefix, " feed-forward norm")
    )
    ffn_node <- peek()
    if (is.null(ffn_node) || !(ffn_node$op %in% feed_forward_ops)) {
      stop(prefix, " has no supported feed-forward operator")
    }
    cursor <- cursor + 1L
    expect_inputs(
      ffn_node,
      ffn_norm_node$id,
      paste0(prefix, " feed-forward")
    )
    feed_forward <- c(
      list(op = ffn_node$op),
      ffn_node$attributes
    )
    ffn_post <- post_norm(
      ffn_node$id,
      paste0(prefix, " feed-forward post-norm")
    )
    joined <- take("add", paste0(prefix, " feed-forward residual"))
    expect_inputs(
      joined,
      c(residual$id, ffn_post$id),
      paste0(prefix, " feed-forward residual")
    )

    eps <- c(
      eps,
      operator_norm$eps,
      operator_post$eps,
      ffn_norm$eps,
      ffn_post$eps
    )
    layers[[layer_index + 1L]] <- list(
      index = as.integer(layer_index),
      operator_norm = operator_norm$weight,
      operator = .rllm_lower_attributes(operator),
      operator_post_norm = operator_post$weight,
      ffn_norm = ffn_norm$weight,
      feed_forward = .rllm_lower_attributes(feed_forward),
      feed_forward_post_norm = ffn_post$weight,
      state = .rllm_lower_attributes(state)
    )
    current <- joined$id
  }
  if (!length(layers)) {
    stop("native GGML programs must contain at least one transformer layer")
  }

  output_norm_node <- take("rms_norm", "program output norm")
  output_norm <- norm(output_norm_node, current, "program output norm")
  eps <- c(eps, output_norm$eps)
  eps <- unique(eps[!is.na(eps)])
  if (length(eps) != 1L) {
    stop("native GGML lowering requires one RMS normalization epsilon")
  }
  current <- output_norm_node$id

  output_node <- peek()
  if (is.null(output_node)) {
    stop("program has no output operator")
  }
  if (identical(output_node$op, "linear")) {
    cursor <- cursor + 1L
    expect_inputs(output_node, current, "output projection")
    if (!is.null(output_node$attributes$bias)) {
      stop("output projection bias is unsupported by the GGML lowering")
    }
    weight <- .rllm_lower_parameter(
      output_node$attributes$weight,
      "output projection weight"
    )
    output <- list(
      op = "projection",
      norm = output_norm$weight,
      weight = weight,
      tied = identical(weight, embedding_name)
    )
    current <- output_node$id
  } else if (identical(output_node$op, "pool")) {
    cursor <- cursor + 1L
    expect_inputs(output_node, current, "output pooling")
    pooling <- output_node$attributes$kind
    if (
      !is.character(pooling) ||
        length(pooling) != 1L ||
        !(pooling %in% c("mean", "none"))
    ) {
      stop("GGML output pooling must be 'mean' or 'none'")
    }
    current <- output_node$id
    projections <- character()
    while (!is.null(peek()) && identical(peek()$op, "linear")) {
      projection <- peek()
      cursor <- cursor + 1L
      expect_inputs(projection, current, "embedding projection")
      if (!is.null(projection$attributes$bias)) {
        stop("embedding projection bias is unsupported by the GGML lowering")
      }
      projections <- c(
        projections,
        .rllm_lower_parameter(
          projection$attributes$weight,
          "embedding projection weight"
        )
      )
      current <- projection$id
    }
    if (!(length(projections) %in% c(0L, 2L))) {
      stop("GGML embedding output needs zero or two projections")
    }
    final <- nodes[[match(current, ids)]]
    output <- list(
      op = "embedding",
      norm = output_norm$weight,
      pooling = pooling,
      projection_1 = if (length(projections)) projections[[1L]] else NULL,
      projection_2 = if (length(projections)) projections[[2L]] else NULL,
      dimension = integer_dimension(
        final$shape[[1L]],
        "embedding output dimension"
      )
    )
  } else {
    stop("unsupported native output operator '", output_node$op, "'")
  }

  if (cursor <= length(nodes)) {
    stop("native output is followed by operator '", nodes[[cursor]]$op, "'")
  }
  if (
    length(program$outputs) != 1L ||
      !identical(unlist(program$outputs, use.names = FALSE), current)
  ) {
    stop("native GGML programs must expose exactly their final value")
  }

  widths <- vapply(
    layers,
    function(layer) {
      as.integer(layer$feed_forward$width)
    },
    integer(1)
  )
  if (anyNA(widths) || any(widths < 1L)) {
    stop("feed-forward widths must be positive integers")
  }
  heads <- unlist(
    lapply(layers, function(layer) {
      operator <- layer$operator
      fields <- intersect(
        c("n_head", "value_heads", "key_heads"),
        names(operator)
      )
      unlist(operator[fields], use.names = FALSE)
    }),
    use.names = FALSE
  )
  heads <- as.integer(heads)
  heads <- heads[!is.na(heads) & heads > 0L]

  resolved <- symbols
  resolved$n_layer <- as.integer(length(layers))
  resolved$n_embd <- n_embd
  resolved$n_head <- if (length(heads)) max(heads) else 1L
  resolved$n_ff <- max(widths)
  resolved$n_vocab <- n_vocab
  resolved$rms_eps <- eps[[1L]]

  structure(
    list(
      name = program$name,
      symbols = resolved,
      input = input,
      layers = layers,
      output = output
    ),
    class = c("rllm_ggml_lowering", "list")
  )
}

.rllm_bind_program <- function(program, symbols, bindings) {
  if (
    !is.list(bindings) ||
      is.null(names(bindings)) ||
      anyNA(names(bindings)) ||
      any(!nzchar(names(bindings))) ||
      anyDuplicated(names(bindings))
  ) {
    stop("program bindings must be a uniquely named list")
  }
  declared <- names(program$parameters)
  missing <- setdiff(declared, names(bindings))
  extra <- setdiff(names(bindings), declared)
  if (length(missing)) {
    stop("missing program bindings: ", paste(missing, collapse = ", "))
  }
  if (length(extra)) {
    stop("undeclared program bindings: ", paste(extra, collapse = ", "))
  }
  for (name in declared) {
    binding <- bindings[[name]]
    if (
      !is.list(binding) ||
        is.null(binding$payload) ||
        !is.character(binding$type) ||
        length(binding$type) != 1L ||
        !is.numeric(binding$dims)
    ) {
      stop(
        "program binding '",
        name,
        "' must contain payload, type and dimensions"
      )
    }
    expected <- program$parameters[[name]]$shape
    if (!identical(as.integer(binding$dims), as.integer(expected))) {
      stop(
        "program binding '",
        name,
        "' has dimensions [",
        paste(binding$dims, collapse = ", "),
        "], expected [",
        paste(expected, collapse = ", "),
        "]"
      )
    }
  }
  structure(
    list(
      program = program,
      lowering = .rllm_lower_program(program, symbols),
      bindings = bindings
    ),
    class = c("rllm_bound_program", "list")
  )
}
