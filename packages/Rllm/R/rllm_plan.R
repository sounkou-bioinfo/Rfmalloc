#' Inspect the semantic execution plan of a GGUF model
#'
#' Rllm normalizes model-family metadata into a small data AST before it
#' borrows any weight payload. The plan names tensor roles, resolved shapes,
#' layer operators, feed-forward operators, persistent state and output
#' projection. It contains data only: no R closures and no backend objects.
#'
#' @param x An `rllm_model` or a path to a GGUF file.
#' @return An inspectable object of class `rllm_plan`.
#' @export
rllm_plan <- function(x) {
    if (inherits(x, "rllm_model")) {
        return(x$plan)
    }
    if (!is.character(x) || length(x) != 1L || is.na(x)) {
        stop("x must be an rllm_model or one GGUF path")
    }
    .rllm_plan_from_gguf(
        Rgguf::gguf_metadata(x),
        Rgguf::gguf_tensors(x)
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
        stop("GGUF hyperparameter '", name, "' must contain non-negative integers")
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

.rllm_tensor <- function(name, role, shape) {
    structure(list(
        name = name,
        role = role,
        shape = as.integer(shape)
    ), class = "rllm_tensor_binding")
}

.rllm_add_tensor <- function(tensors, binding) {
    if (!is.null(tensors[[binding$name]])) {
        old <- tensors[[binding$name]]
        if (!identical(old$shape, binding$shape)) {
            stop("tensor '", binding$name, "' was assigned conflicting shapes")
        }
        return(tensors)
    }
    tensors[[binding$name]] <- binding
    tensors
}

.rllm_validate_plan <- function(plan, directory) {
    if (!is.data.frame(directory) || !all(c("name", "dims") %in% names(directory))) {
        stop("GGUF tensor directory must contain name and dims columns")
    }
    if (anyDuplicated(directory$name)) {
        stop("GGUF tensor directory contains duplicate names")
    }
    for (binding in plan$tensors) {
        at <- match(binding$name, directory$name)
        if (is.na(at)) {
            stop("missing tensor '", binding$name, "' for role '",
                 binding$role, "'")
        }
        actual <- as.integer(directory$dims[[at]])
        if (!identical(actual, binding$shape)) {
            stop(
                "tensor '", binding$name, "' for role '", binding$role,
                "' has shape [", paste(actual, collapse = ", "),
                "], expected [", paste(binding$shape, collapse = ", "), "]"
            )
        }
    }
    plan
}

.rllm_plan_from_gguf <- function(metadata, directory, rope_mode = NULL) {
    architecture <- metadata[["general.architecture"]]
    if (!is.character(architecture) || length(architecture) != 1L ||
        is.na(architecture) || !nzchar(architecture)) {
        stop("GGUF file has no 'general.architecture' metadata key")
    }
    plan <- switch(
        architecture,
        llama = .rllm_plan_llama(metadata, directory, rope_mode),
        qwen35 = .rllm_plan_qwen35(metadata, directory, rope_mode),
        lfm2moe = .rllm_plan_lfm2moe(metadata, directory, rope_mode),
        `gemma-embedding` = .rllm_plan_gemma_embedding(
            metadata, directory, rope_mode
        ),
        stop("unsupported GGUF architecture '", architecture,
             "': no semantic plan is registered")
    )
    class(plan) <- c("rllm_plan", "list")
    plan <- .rllm_validate_plan(plan, directory)
    plan$program <- .rllm_program_from_plan(plan)
    plan
}

.rllm_plan_llama <- function(metadata, directory, rope_mode) {
    arch <- "llama"
    key <- function(name, default = .rllm_missing) {
        .rllm_metadata(metadata, arch, name, default)
    }
    n_layer <- .rllm_positive(key("block_count"), "llama.block_count")
    n_embd <- .rllm_positive(key("embedding_length"),
                             "llama.embedding_length")
    n_head <- .rllm_positive(key("attention.head_count"),
                             "llama.attention.head_count")
    n_head_kv <- .rllm_positive(key("attention.head_count_kv", n_head),
                                "llama.attention.head_count_kv")
    n_ff <- .rllm_positive(key("feed_forward_length"),
                           "llama.feed_forward_length")
    if (length(n_layer) != 1L || length(n_embd) != 1L ||
        length(n_head) != 1L || length(n_ff) != 1L) {
        stop("llama scalar hyperparameters must have length one")
    }
    if (length(n_head_kv) == 1L) {
        n_head_kv <- rep.int(n_head_kv, n_layer)
    }
    if (length(n_head_kv) != n_layer || any(n_head_kv < 1L)) {
        stop("llama attention.head_count_kv must have length one or block_count")
    }
    if (n_embd %% n_head != 0L || any(n_head %% n_head_kv != 0L)) {
        stop("llama embedding/head dimensions are inconsistent")
    }
    head_dim <- n_embd %/% n_head
    rms_eps <- as.numeric(key("attention.layer_norm_rms_epsilon", 1e-5))
    rope_base <- as.numeric(key("rope.freq_base", 10000))
    rope_dims <- .rllm_positive(key("rope.dimension_count", head_dim),
                                "llama.rope.dimension_count")
    if (length(rms_eps) != 1L || !is.finite(rms_eps) || rms_eps <= 0 ||
        length(rope_base) != 1L || !is.finite(rope_base) || rope_base <= 0 ||
        length(rope_dims) != 1L || rope_dims > head_dim) {
        stop("invalid llama normalization or RoPE parameters")
    }
    if (is.null(rope_mode)) rope_mode <- 0L
    rope_mode <- as.integer(rope_mode)
    if (length(rope_mode) != 1L || is.na(rope_mode) ||
        !rope_mode %in% c(0L, 2L)) {
        stop("rope_mode must be 0 (normal) or 2 (NEOX)")
    }

    emb <- directory[match("token_embd.weight", directory$name), , drop = FALSE]
    if (!nrow(emb)) stop("GGUF file has no 'token_embd.weight' tensor")
    emb_shape <- as.integer(emb$dims[[1L]])
    if (length(emb_shape) != 2L || emb_shape[[1L]] != n_embd) {
        stop("token_embd.weight is inconsistent with llama.embedding_length")
    }
    n_vocab <- emb_shape[[2L]]
    tensors <- list()
    tensors <- .rllm_add_tensor(tensors, .rllm_tensor(
        "token_embd.weight", "token_embedding", c(n_embd, n_vocab)))
    tensors <- .rllm_add_tensor(tensors, .rllm_tensor(
        "output_norm.weight", "output_norm", n_embd))
    output_weight <- if ("output.weight" %in% directory$name) {
        tensors <- .rllm_add_tensor(tensors, .rllm_tensor(
            "output.weight", "output_projection", c(n_embd, n_vocab)))
        "output.weight"
    } else {
        "token_embd.weight"
    }

    layers <- vector("list", n_layer)
    for (index in seq_len(n_layer)) {
        il <- index - 1L
        prefix <- paste0("blk.", il, ".")
        bind <- function(suffix, role, shape) {
            .rllm_tensor(paste0(prefix, suffix), paste0("layer.", il, ".", role),
                         shape)
        }
        kv_dim <- head_dim * n_head_kv[[index]]
        bindings <- list(
            bind("attn_norm.weight", "operator_norm", n_embd),
            bind("attn_q.weight", "attention.query", c(n_embd, n_embd)),
            bind("attn_k.weight", "attention.key", c(n_embd, kv_dim)),
            bind("attn_v.weight", "attention.value", c(n_embd, kv_dim)),
            bind("attn_output.weight", "attention.output", c(n_embd, n_embd)),
            bind("ffn_norm.weight", "ffn_norm", n_embd),
            bind("ffn_gate.weight", "ffn.gate", c(n_embd, n_ff)),
            bind("ffn_up.weight", "ffn.up", c(n_embd, n_ff)),
            bind("ffn_down.weight", "ffn.down", c(n_ff, n_embd))
        )
        for (binding in bindings) tensors <- .rllm_add_tensor(tensors, binding)
        layers[[index]] <- list(
            index = il,
            operator_norm = bindings[[1L]]$name,
            operator = list(
                op = "attention",
                query = bindings[[2L]]$name,
                key = bindings[[3L]]$name,
                value = bindings[[4L]]$name,
                output = bindings[[5L]]$name,
                query_norm = NULL,
                key_norm = NULL,
                n_head = n_head,
                n_head_kv = n_head_kv[[index]],
                head_dim = head_dim,
                rope = list(mode = rope_mode, dims = rope_dims,
                            base = rope_base),
                scale = list(at = "logits", value = 1 / sqrt(head_dim)),
                mask = list(type = "causal", window = 0L)
            ),
            operator_post_norm = NULL,
            ffn_norm = bindings[[6L]]$name,
            feed_forward = list(
                op = "swiglu",
                gate = bindings[[7L]]$name,
                up = bindings[[8L]]$name,
                down = bindings[[9L]]$name,
                width = n_ff
            ),
            feed_forward_post_norm = NULL,
            state = list(op = "kv", width = kv_dim)
        )
    }

    list(
        architecture = arch,
        symbols = list(
            n_layer = n_layer, n_embd = n_embd, n_head = n_head,
            n_head_kv = n_head_kv, n_ff = n_ff, n_vocab = n_vocab,
            rms_eps = rms_eps, rope_base = rope_base, rope_dims = rope_dims
        ),
        input = list(op = "embedding", weight = "token_embd.weight", scale = 1),
        layers = layers,
        output = list(op = "projection", norm = "output_norm.weight",
                      weight = output_weight, tied = output_weight == "token_embd.weight"),
        tensors = tensors
    )
}

.rllm_plan_qwen35 <- function(metadata, directory, rope_mode) {
    arch <- "qwen35"
    key <- function(name, default = .rllm_missing) {
        .rllm_metadata(metadata, arch, name, default)
    }
    scalar <- function(name, default = .rllm_missing) {
        value <- .rllm_positive(key(name, default), paste0(arch, ".", name))
        if (length(value) != 1L) {
            stop("GGUF hyperparameter '", paste0(arch, ".", name),
                 "' must have length one")
        }
        value
    }

    n_layer <- scalar("block_count")
    n_embd <- scalar("embedding_length")
    n_head <- scalar("attention.head_count")
    n_head_kv <- scalar("attention.head_count_kv")
    n_ff <- scalar("feed_forward_length")
    head_dim <- scalar("attention.key_length")
    value_dim <- scalar("attention.value_length")
    conv_kernel <- scalar("ssm.conv_kernel")
    state_dim <- scalar("ssm.state_size")
    group_count <- scalar("ssm.group_count")
    value_heads <- scalar("ssm.time_step_rank")
    inner_dim <- scalar("ssm.inner_size")
    full_interval <- scalar("full_attention_interval", 4L)
    rope_dims <- scalar("rope.dimension_count", head_dim)
    rope_sections <- .rllm_integer(
        key("rope.dimension_sections"),
        "qwen35.rope.dimension_sections"
    )
    rms_eps <- as.numeric(key("attention.layer_norm_rms_epsilon", 1e-6))
    rope_base <- as.numeric(key("rope.freq_base", 1e7))

    if (n_head %% n_head_kv != 0L || head_dim != value_dim ||
        inner_dim != n_head * head_dim || inner_dim %% value_heads != 0L ||
        inner_dim %/% value_heads != state_dim ||
        value_heads %% group_count != 0L) {
        stop("qwen35 attention and recurrent-state dimensions are inconsistent")
    }
    if (conv_kernel < 2L || length(rope_sections) != 4L ||
        sum(rope_sections) * 2L != rope_dims ||
        length(rms_eps) != 1L || !is.finite(rms_eps) || rms_eps <= 0 ||
        length(rope_base) != 1L || !is.finite(rope_base) || rope_base <= 0) {
        stop("invalid qwen35 convolution, normalization, or MRoPE parameters")
    }
    if (!is.null(rope_mode)) {
        stop("qwen35 uses metadata-defined MRoPE; rope_mode cannot be overridden")
    }

    recurrent <- key("attention.recurrent_layers", NULL)
    if (is.null(recurrent)) {
        recurrent <- seq_len(n_layer) %% full_interval != 0L
    } else {
        if (!(is.logical(recurrent) || is.numeric(recurrent)) ||
            length(recurrent) != n_layer || anyNA(recurrent) ||
            any(!recurrent %in% c(FALSE, TRUE, 0, 1))) {
            stop("qwen35.attention.recurrent_layers must contain one flag per block")
        }
        recurrent <- as.logical(recurrent)
    }

    emb <- directory[match("token_embd.weight", directory$name), , drop = FALSE]
    if (!nrow(emb)) stop("GGUF file has no 'token_embd.weight' tensor")
    emb_shape <- as.integer(emb$dims[[1L]])
    if (length(emb_shape) != 2L || emb_shape[[1L]] != n_embd) {
        stop("token_embd.weight is inconsistent with qwen35.embedding_length")
    }
    n_vocab <- emb_shape[[2L]]
    tensors <- list()
    tensors <- .rllm_add_tensor(tensors, .rllm_tensor(
        "token_embd.weight", "token_embedding", c(n_embd, n_vocab)))
    tensors <- .rllm_add_tensor(tensors, .rllm_tensor(
        "output_norm.weight", "output_norm", n_embd))
    output_weight <- if ("output.weight" %in% directory$name) {
        tensors <- .rllm_add_tensor(tensors, .rllm_tensor(
            "output.weight", "output_projection", c(n_embd, n_vocab)))
        "output.weight"
    } else {
        "token_embd.weight"
    }

    kv_dim <- head_dim * n_head_kv
    key_width <- state_dim * group_count
    conv_width <- 2L * key_width + inner_dim
    layers <- vector("list", n_layer)
    for (index in seq_len(n_layer)) {
        il <- index - 1L
        prefix <- paste0("blk.", il, ".")
        bind <- function(suffix, role, shape) {
            .rllm_tensor(paste0(prefix, suffix), paste0("layer.", il, ".", role),
                         shape)
        }
        norm <- bind("attn_norm.weight", "operator_norm", n_embd)
        ffn_norm <- bind("post_attention_norm.weight", "ffn_norm", n_embd)
        ffn <- list(
            bind("ffn_gate.weight", "ffn.gate", c(n_embd, n_ff)),
            bind("ffn_up.weight", "ffn.up", c(n_embd, n_ff)),
            bind("ffn_down.weight", "ffn.down", c(n_ff, n_embd))
        )
        tensors <- .rllm_add_tensor(tensors, norm)
        tensors <- .rllm_add_tensor(tensors, ffn_norm)
        for (binding in ffn) tensors <- .rllm_add_tensor(tensors, binding)

        if (recurrent[[index]]) {
            op_bindings <- list(
                bind("attn_qkv.weight", "gated_delta.qkv",
                     c(n_embd, conv_width)),
                bind("attn_gate.weight", "gated_delta.output_gate",
                     c(n_embd, inner_dim)),
                bind("ssm_conv1d.weight", "gated_delta.convolution",
                     c(conv_kernel, conv_width)),
                bind("ssm_dt.bias", "gated_delta.time_bias", value_heads),
                bind("ssm_a", "gated_delta.decay", value_heads),
                bind("ssm_beta.weight", "gated_delta.beta",
                     c(n_embd, value_heads)),
                bind("ssm_alpha.weight", "gated_delta.alpha",
                     c(n_embd, value_heads)),
                bind("ssm_norm.weight", "gated_delta.output_norm", state_dim),
                bind("ssm_out.weight", "gated_delta.output",
                     c(inner_dim, n_embd))
            )
            for (binding in op_bindings) {
                tensors <- .rllm_add_tensor(tensors, binding)
            }
            operator <- list(
                op = "gated_delta_net",
                qkv = op_bindings[[1L]]$name,
                output_gate = op_bindings[[2L]]$name,
                convolution = op_bindings[[3L]]$name,
                time_bias = op_bindings[[4L]]$name,
                decay = op_bindings[[5L]]$name,
                beta = op_bindings[[6L]]$name,
                alpha = op_bindings[[7L]]$name,
                norm = op_bindings[[8L]]$name,
                output = op_bindings[[9L]]$name,
                key_heads = group_count,
                value_heads = value_heads,
                key_head_dim = state_dim,
                value_head_dim = state_dim,
                convolution_width = conv_width,
                convolution_kernel = conv_kernel,
                qk_norm = list(kind = "l2", eps = rms_eps),
                activations = list(
                    convolution = "silu", beta = "sigmoid",
                    time_step = "softplus", output_gate = "silu"
                )
            )
            state <- list(
                op = "gated_delta",
                matrix = c(row = state_dim, column = state_dim,
                           head = value_heads),
                convolution = c(width = conv_width,
                                history = conv_kernel - 1L)
            )
        } else {
            op_bindings <- list(
                bind("attn_q.weight", "attention.query_gate",
                     c(n_embd, 2L * inner_dim)),
                bind("attn_k.weight", "attention.key", c(n_embd, kv_dim)),
                bind("attn_v.weight", "attention.value", c(n_embd, kv_dim)),
                bind("attn_output.weight", "attention.output",
                     c(inner_dim, n_embd)),
                bind("attn_q_norm.weight", "attention.query_norm", head_dim),
                bind("attn_k_norm.weight", "attention.key_norm", head_dim)
            )
            for (binding in op_bindings) {
                tensors <- .rllm_add_tensor(tensors, binding)
            }
            operator <- list(
                op = "gated_attention",
                query_gate = op_bindings[[1L]]$name,
                key = op_bindings[[2L]]$name,
                value = op_bindings[[3L]]$name,
                output = op_bindings[[4L]]$name,
                query_norm = op_bindings[[5L]]$name,
                key_norm = op_bindings[[6L]]$name,
                query_gate_layout = "head_interleaved",
                gate_activation = "sigmoid",
                n_head = n_head,
                n_head_kv = n_head_kv,
                head_dim = head_dim,
                rope = list(mode = "mrope", dims = rope_dims,
                            sections = rope_sections, base = rope_base),
                scale = list(at = "logits", value = 1 / sqrt(head_dim)),
                mask = list(type = "causal", window = 0L)
            )
            state <- list(op = "kv", width = kv_dim)
        }

        layers[[index]] <- list(
            index = il,
            operator_norm = norm$name,
            operator = operator,
            operator_post_norm = NULL,
            ffn_norm = ffn_norm$name,
            feed_forward = list(
                op = "swiglu", gate = ffn[[1L]]$name,
                up = ffn[[2L]]$name, down = ffn[[3L]]$name,
                width = n_ff
            ),
            feed_forward_post_norm = NULL,
            state = state
        )
    }

    list(
        architecture = arch,
        symbols = list(
            n_layer = n_layer, n_embd = n_embd, n_head = n_head,
            n_head_kv = n_head_kv, n_ff = n_ff, n_vocab = n_vocab,
            head_dim = head_dim, rms_eps = rms_eps, rope_base = rope_base,
            rope_dims = rope_dims, rope_sections = rope_sections,
            recurrent = recurrent, full_attention_interval = full_interval,
            ssm_state_size = state_dim, ssm_group_count = group_count,
            ssm_value_heads = value_heads, ssm_inner_size = inner_dim,
            ssm_conv_kernel = conv_kernel
        ),
        input = list(op = "embedding", weight = "token_embd.weight", scale = 1),
        layers = layers,
        output = list(op = "projection", norm = "output_norm.weight",
                      weight = output_weight,
                      tied = output_weight == "token_embd.weight"),
        tensors = tensors
    )
}

.rllm_plan_lfm2moe <- function(metadata, directory, rope_mode) {
    arch <- "lfm2moe"
    key <- function(name, default = .rllm_missing) {
        .rllm_metadata(metadata, arch, name, default)
    }
    scalar <- function(name, positive = TRUE, default = .rllm_missing) {
        value <- if (positive) {
            .rllm_positive(key(name, default), paste0(arch, ".", name))
        } else {
            .rllm_integer(key(name, default), paste0(arch, ".", name))
        }
        if (length(value) != 1L) {
            stop("GGUF hyperparameter '", paste0(arch, ".", name),
                 "' must have length one")
        }
        value
    }
    n_layer <- scalar("block_count")
    n_embd <- scalar("embedding_length")
    n_head <- scalar("attention.head_count")
    n_ff <- scalar("feed_forward_length")
    n_head_kv <- .rllm_integer(key("attention.head_count_kv"),
                               "lfm2moe.attention.head_count_kv")
    if (length(n_head_kv) != n_layer) {
        stop("lfm2moe.attention.head_count_kv must have block_count entries")
    }
    if (n_embd %% n_head != 0L ||
        any(n_head_kv > 0L & n_head %% n_head_kv != 0L)) {
        stop("lfm2moe embedding/head dimensions are inconsistent")
    }
    head_dim <- n_embd %/% n_head
    n_expert <- scalar("expert_count")
    n_expert_used <- scalar("expert_used_count")
    n_ff_expert <- scalar("expert_feed_forward_length")
    n_dense <- scalar("leading_dense_block_count", positive = FALSE, default = 0)
    l_cache <- scalar("shortconv.l_cache")
    gating <- scalar("expert_gating_func", positive = FALSE)
    if (n_expert_used > n_expert || n_dense > n_layer || l_cache < 2L) {
        stop("invalid lfm2moe expert, dense-layer, or short-convolution parameters")
    }
    if (gating != 2L) {
        stop("lfm2moe expert_gating_func ", gating,
             " is unsupported; the plan vocabulary expects sigmoid routing")
    }
    rms_eps <- as.numeric(key("attention.layer_norm_rms_epsilon", 1e-5))
    rope_base <- as.numeric(key("rope.freq_base", 5e6))
    if (length(rms_eps) != 1L || !is.finite(rms_eps) || rms_eps <= 0 ||
        length(rope_base) != 1L || !is.finite(rope_base) || rope_base <= 0) {
        stop("invalid lfm2moe normalization or RoPE parameters")
    }
    if (is.null(rope_mode)) rope_mode <- 2L
    rope_mode <- as.integer(rope_mode)
    if (length(rope_mode) != 1L || is.na(rope_mode) || rope_mode != 2L) {
        stop("lfm2moe uses NEOX RoPE; rope_mode must be 2")
    }

    emb <- directory[match("token_embd.weight", directory$name), , drop = FALSE]
    if (!nrow(emb)) stop("GGUF file has no 'token_embd.weight' tensor")
    emb_shape <- as.integer(emb$dims[[1L]])
    if (length(emb_shape) != 2L || emb_shape[[1L]] != n_embd) {
        stop("token_embd.weight is inconsistent with lfm2moe.embedding_length")
    }
    n_vocab <- emb_shape[[2L]]
    tensors <- list()
    tensors <- .rllm_add_tensor(tensors, .rllm_tensor(
        "token_embd.weight", "token_embedding", c(n_embd, n_vocab)))
    tensors <- .rllm_add_tensor(tensors, .rllm_tensor(
        "token_embd_norm.weight", "output_norm", n_embd))
    output_weight <- if ("output.weight" %in% directory$name) {
        tensors <- .rllm_add_tensor(tensors, .rllm_tensor(
            "output.weight", "output_projection", c(n_embd, n_vocab)))
        "output.weight"
    } else {
        "token_embd.weight"
    }

    layers <- vector("list", n_layer)
    for (index in seq_len(n_layer)) {
        il <- index - 1L
        prefix <- paste0("blk.", il, ".")
        bind <- function(suffix, role, shape) {
            .rllm_tensor(paste0(prefix, suffix), paste0("layer.", il, ".", role),
                         shape)
        }
        norm <- bind("attn_norm.weight", "operator_norm", n_embd)
        ffn_norm <- bind("ffn_norm.weight", "ffn_norm", n_embd)
        tensors <- .rllm_add_tensor(tensors, norm)
        tensors <- .rllm_add_tensor(tensors, ffn_norm)

        if (n_head_kv[[index]] == 0L) {
            op_bindings <- list(
                bind("shortconv.in_proj.weight", "shortconv.input",
                     c(n_embd, 3L * n_embd)),
                bind("shortconv.conv.weight", "shortconv.kernel",
                     c(l_cache, n_embd)),
                bind("shortconv.out_proj.weight", "shortconv.output",
                     c(n_embd, n_embd))
            )
            for (binding in op_bindings) tensors <- .rllm_add_tensor(tensors, binding)
            operator <- list(
                op = "shortconv",
                input = op_bindings[[1L]]$name,
                kernel = op_bindings[[2L]]$name,
                output = op_bindings[[3L]]$name,
                width = n_embd,
                l_cache = l_cache
            )
            state <- list(op = "conv", width = n_embd,
                          history = l_cache - 1L)
        } else {
            kv_dim <- head_dim * n_head_kv[[index]]
            op_bindings <- list(
                bind("attn_q.weight", "attention.query", c(n_embd, n_embd)),
                bind("attn_k.weight", "attention.key", c(n_embd, kv_dim)),
                bind("attn_v.weight", "attention.value", c(n_embd, kv_dim)),
                bind("attn_output.weight", "attention.output", c(n_embd, n_embd)),
                bind("attn_q_norm.weight", "attention.query_norm", head_dim),
                bind("attn_k_norm.weight", "attention.key_norm", head_dim)
            )
            for (binding in op_bindings) tensors <- .rllm_add_tensor(tensors, binding)
            operator <- list(
                op = "attention",
                query = op_bindings[[1L]]$name,
                key = op_bindings[[2L]]$name,
                value = op_bindings[[3L]]$name,
                output = op_bindings[[4L]]$name,
                query_norm = op_bindings[[5L]]$name,
                key_norm = op_bindings[[6L]]$name,
                n_head = n_head,
                n_head_kv = n_head_kv[[index]],
                head_dim = head_dim,
                rope = list(mode = rope_mode, dims = head_dim,
                            base = rope_base),
                scale = list(at = "logits", value = 1 / sqrt(head_dim)),
                mask = list(type = "causal", window = 0L)
            )
            state <- list(op = "kv", width = kv_dim)
        }

        if (il < n_dense) {
            ffn_bindings <- list(
                bind("ffn_gate.weight", "ffn.gate", c(n_embd, n_ff)),
                bind("ffn_up.weight", "ffn.up", c(n_embd, n_ff)),
                bind("ffn_down.weight", "ffn.down", c(n_ff, n_embd))
            )
            for (binding in ffn_bindings) tensors <- .rllm_add_tensor(tensors, binding)
            feed_forward <- list(
                op = "swiglu",
                gate = ffn_bindings[[1L]]$name,
                up = ffn_bindings[[2L]]$name,
                down = ffn_bindings[[3L]]$name,
                width = n_ff
            )
        } else {
            ffn_bindings <- list(
                bind("ffn_gate_inp.weight", "moe.router", c(n_embd, n_expert)),
                bind("exp_probs_b.bias", "moe.selection_bias", n_expert),
                bind("ffn_gate_exps.weight", "moe.gate_experts",
                     c(n_embd, n_ff_expert, n_expert)),
                bind("ffn_up_exps.weight", "moe.up_experts",
                     c(n_embd, n_ff_expert, n_expert)),
                bind("ffn_down_exps.weight", "moe.down_experts",
                     c(n_ff_expert, n_embd, n_expert))
            )
            for (binding in ffn_bindings) tensors <- .rllm_add_tensor(tensors, binding)
            feed_forward <- list(
                op = "moe_swiglu",
                router = ffn_bindings[[1L]]$name,
                selection_bias = ffn_bindings[[2L]]$name,
                gate = ffn_bindings[[3L]]$name,
                up = ffn_bindings[[4L]]$name,
                down = ffn_bindings[[5L]]$name,
                routing = "sigmoid",
                experts = n_expert,
                selected = n_expert_used,
                width = n_ff_expert,
                normalize_selected = TRUE,
                scale = 1
            )
        }

        layers[[index]] <- list(
            index = il,
            operator_norm = norm$name,
            operator = operator,
            operator_post_norm = NULL,
            ffn_norm = ffn_norm$name,
            feed_forward = feed_forward,
            feed_forward_post_norm = NULL,
            state = state
        )
    }

    list(
        architecture = arch,
        symbols = list(
            n_layer = n_layer, n_embd = n_embd, n_head = n_head,
            n_head_kv = n_head_kv, n_ff = n_ff, n_vocab = n_vocab,
            rms_eps = rms_eps, rope_base = rope_base, rope_dims = head_dim,
            n_expert = n_expert, n_expert_used = n_expert_used,
            n_ff_expert = n_ff_expert, n_dense = n_dense,
            shortconv_l_cache = l_cache
        ),
        input = list(op = "embedding", weight = "token_embd.weight", scale = 1),
        layers = layers,
        output = list(op = "projection", norm = "token_embd_norm.weight",
                      weight = output_weight, tied = output_weight == "token_embd.weight"),
        tensors = tensors
    )
}

.rllm_plan_gemma_embedding <- function(metadata, directory, rope_mode) {
    arch <- "gemma-embedding"
    key <- function(name, default = .rllm_missing) {
        .rllm_metadata(metadata, arch, name, default)
    }
    scalar <- function(name, positive = TRUE, default = .rllm_missing) {
        value <- if (positive) {
            .rllm_positive(key(name, default), paste0(arch, ".", name))
        } else {
            .rllm_integer(key(name, default), paste0(arch, ".", name))
        }
        if (length(value) != 1L) {
            stop("GGUF hyperparameter '", paste0(arch, ".", name),
                 "' must have length one")
        }
        value
    }

    n_layer <- scalar("block_count")
    n_embd <- scalar("embedding_length")
    n_head <- scalar("attention.head_count")
    n_head_kv <- scalar("attention.head_count_kv")
    n_ff <- scalar("feed_forward_length")
    head_dim <- scalar("attention.key_length", default = n_embd %/% n_head)
    value_dim <- scalar("attention.value_length", default = head_dim)
    if (n_embd %% n_head != 0L || head_dim * n_head != n_embd ||
        value_dim != head_dim || n_head %% n_head_kv != 0L) {
        stop("gemma-embedding attention dimensions are inconsistent")
    }
    rms_eps <- as.numeric(key("attention.layer_norm_rms_epsilon", 1e-6))
    rope_base <- as.numeric(key("rope.freq_base", 1e6))
    rope_base_swa <- as.numeric(key("rope.freq_base_swa", 10000))
    sliding_window <- scalar("attention.sliding_window")
    sliding_period <- scalar("attention.sliding_window_pattern", default = 6)
    pooling <- scalar("pooling_type", positive = FALSE)
    if (length(rms_eps) != 1L || !is.finite(rms_eps) || rms_eps <= 0 ||
        length(rope_base) != 1L || !is.finite(rope_base) || rope_base <= 0 ||
        length(rope_base_swa) != 1L || !is.finite(rope_base_swa) ||
        rope_base_swa <= 0 || sliding_window < 2L || pooling != 1L) {
        stop("invalid gemma-embedding normalization, attention, or pooling parameters")
    }
    if (is.null(rope_mode)) rope_mode <- 2L
    rope_mode <- as.integer(rope_mode)
    if (length(rope_mode) != 1L || is.na(rope_mode) || rope_mode != 2L) {
        stop("gemma-embedding uses NEOX RoPE; rope_mode must be 2")
    }

    emb <- directory[match("token_embd.weight", directory$name), , drop = FALSE]
    if (!nrow(emb)) stop("GGUF file has no 'token_embd.weight' tensor")
    emb_shape <- as.integer(emb$dims[[1L]])
    if (length(emb_shape) != 2L || emb_shape[[1L]] != n_embd) {
        stop("token_embd.weight is inconsistent with gemma-embedding.embedding_length")
    }
    n_vocab <- emb_shape[[2L]]
    tensors <- list()
    tensors <- .rllm_add_tensor(tensors, .rllm_tensor(
        "token_embd.weight", "token_embedding", c(n_embd, n_vocab)))
    tensors <- .rllm_add_tensor(tensors, .rllm_tensor(
        "output_norm.weight", "output_norm", n_embd))

    dense_2_in <- scalar("dense_2_feat_in", positive = FALSE, default = 0)
    dense_2_out <- scalar("dense_2_feat_out", positive = FALSE, default = 0)
    dense_3_in <- scalar("dense_3_feat_in", positive = FALSE, default = 0)
    dense_3_out <- scalar("dense_3_feat_out", positive = FALSE, default = 0)
    has_dense <- all(c("dense_2.weight", "dense_3.weight") %in% directory$name)
    if (has_dense) {
        if (dense_2_in != n_embd || dense_2_out < 1L ||
            dense_3_in != dense_2_out || dense_3_out != n_embd) {
            stop("gemma-embedding dense projection metadata are inconsistent")
        }
        tensors <- .rllm_add_tensor(tensors, .rllm_tensor(
            "dense_2.weight", "embedding_projection.1",
            c(dense_2_in, dense_2_out)))
        tensors <- .rllm_add_tensor(tensors, .rllm_tensor(
            "dense_3.weight", "embedding_projection.2",
            c(dense_3_in, dense_3_out)))
        output_dim <- dense_3_out
        projection_1 <- "dense_2.weight"
        projection_2 <- "dense_3.weight"
    } else {
        if (any(c(dense_2_in, dense_2_out, dense_3_in, dense_3_out) != 0L)) {
            stop("gemma-embedding declares dense projections without both tensors")
        }
        output_dim <- n_embd
        projection_1 <- NULL
        projection_2 <- NULL
    }

    layers <- vector("list", n_layer)
    for (index in seq_len(n_layer)) {
        il <- index - 1L
        prefix <- paste0("blk.", il, ".")
        bind <- function(suffix, role, shape) {
            .rllm_tensor(paste0(prefix, suffix), paste0("layer.", il, ".", role),
                         shape)
        }
        bindings <- list(
            bind("attn_norm.weight", "operator_norm", n_embd),
            bind("attn_q.weight", "attention.query", c(n_embd, n_embd)),
            bind("attn_k.weight", "attention.key",
                 c(n_embd, head_dim * n_head_kv)),
            bind("attn_v.weight", "attention.value",
                 c(n_embd, value_dim * n_head_kv)),
            bind("attn_output.weight", "attention.output", c(n_embd, n_embd)),
            bind("attn_q_norm.weight", "attention.query_norm", head_dim),
            bind("attn_k_norm.weight", "attention.key_norm", head_dim),
            bind("post_attention_norm.weight", "operator_post_norm", n_embd),
            bind("ffn_norm.weight", "ffn_norm", n_embd),
            bind("ffn_gate.weight", "ffn.gate", c(n_embd, n_ff)),
            bind("ffn_up.weight", "ffn.up", c(n_embd, n_ff)),
            bind("ffn_down.weight", "ffn.down", c(n_ff, n_embd)),
            bind("post_ffw_norm.weight", "ffn_post_norm", n_embd)
        )
        for (binding in bindings) tensors <- .rllm_add_tensor(tensors, binding)

        is_swa <- il %% sliding_period < sliding_period - 1L
        layers[[index]] <- list(
            index = il,
            operator_norm = bindings[[1L]]$name,
            operator = list(
                op = "attention",
                query = bindings[[2L]]$name,
                key = bindings[[3L]]$name,
                value = bindings[[4L]]$name,
                output = bindings[[5L]]$name,
                query_norm = bindings[[6L]]$name,
                key_norm = bindings[[7L]]$name,
                n_head = n_head,
                n_head_kv = n_head_kv,
                head_dim = head_dim,
                rope = list(mode = rope_mode, dims = head_dim,
                            base = if (is_swa) rope_base_swa else rope_base),
                scale = list(at = "query", value = 1 / sqrt(head_dim)),
                mask = list(
                    type = if (is_swa) "symmetric_window" else "bidirectional",
                    window = if (is_swa) sliding_window else 0L
                )
            ),
            operator_post_norm = bindings[[8L]]$name,
            ffn_norm = bindings[[9L]]$name,
            feed_forward = list(
                op = "geglu",
                gate = bindings[[10L]]$name,
                up = bindings[[11L]]$name,
                down = bindings[[12L]]$name,
                width = n_ff
            ),
            feed_forward_post_norm = bindings[[13L]]$name,
            state = list(op = "none")
        )
    }

    list(
        architecture = arch,
        symbols = list(
            n_layer = n_layer, n_embd = n_embd, n_head = n_head,
            n_head_kv = n_head_kv, n_ff = n_ff, n_vocab = n_vocab,
            rms_eps = rms_eps, rope_base = rope_base, rope_dims = head_dim,
            sliding_window = sliding_window, sliding_period = sliding_period,
            output_dim = output_dim
        ),
        input = list(op = "embedding", weight = "token_embd.weight",
                     scale = sqrt(n_embd)),
        layers = layers,
        output = list(
            op = "embedding", norm = "output_norm.weight", pooling = "mean",
            projection_1 = projection_1, projection_2 = projection_2,
            dimension = output_dim
        ),
        tensors = tensors
    )
}
