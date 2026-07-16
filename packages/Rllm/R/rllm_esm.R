.rllm_esm2_program <- function(n_layer, n_embd, n_head, n_ff, n_vocab,
                               rms_eps, mask_id, padding_id, bos_id, eos_id,
                               token_dropout) {
    n_residue <- "n_residue"
    hidden_shape <- c(
        feature = n_embd, sequence = n_residue, batch = "batch"
    )
    attention_shape <- c(
        head = n_head, query = n_residue, key = n_residue, batch = "batch"
    )
    inputs <- rllm_inputs(
        tokens = c(sequence = n_residue, batch = "batch"),
        padding_mask = c(sequence = n_residue, batch = "batch"),
        .dtype = c(tokens = "i32", padding_mask = "bool"),
        .name = "esm2"
    )
    parameter <- function(name, shape, role) {
        .rllm_architecture_parameter(name, role, shape)
    }
    embedding <- parameter(
        "embed_tokens.weight", c(n_embd, n_vocab), "token_embedding"
    )
    hidden <- rllm_op(
        inputs, "esm_token_embedding", weight = embedding,
        token_dropout = list(
            mask_index = mask_id, training_ratio = token_dropout
        ),
        output_shape = hidden_shape
    ) |>
        rllm_tap("representation.0")

    attentions <- vector("list", n_layer)
    for (index in seq_len(n_layer)) {
        il <- index - 1L
        prefix <- paste0("layers.", il, ".")
        layer_parameter <- function(suffix, shape, role) {
            parameter(
                paste0(prefix, suffix), shape,
                paste0("layer.", il, ".", role)
            )
        }
        self_norm <- layer_parameter(
            "self_attn_layer_norm.weight", n_embd, "attention_norm"
        )
        self_norm_bias <- layer_parameter(
            "self_attn_layer_norm.bias", n_embd, "attention_norm_bias"
        )
        final_norm <- layer_parameter(
            "final_layer_norm.weight", n_embd, "feed_forward_norm"
        )
        final_norm_bias <- layer_parameter(
            "final_layer_norm.bias", n_embd, "feed_forward_norm_bias"
        )
        projection <- function(name) {
            list(
                weight = layer_parameter(
                    paste0("self_attn.", name, "_proj.weight"),
                    c(n_embd, n_embd), paste0("attention.", name)
                ),
                bias = layer_parameter(
                    paste0("self_attn.", name, "_proj.bias"),
                    n_embd, paste0("attention.", name, "_bias")
                )
            )
        }
        query <- projection("q")
        key <- projection("k")
        value <- projection("v")
        output <- projection("out")
        fc1 <- layer_parameter(
            "fc1.weight", c(n_embd, n_ff), "feed_forward.up"
        )
        fc1_bias <- layer_parameter(
            "fc1.bias", n_ff, "feed_forward.up_bias"
        )
        fc2 <- layer_parameter(
            "fc2.weight", c(n_ff, n_embd), "feed_forward.down"
        )
        fc2_bias <- layer_parameter(
            "fc2.bias", n_embd, "feed_forward.down_bias"
        )

        block <- rllm_module(
            paste0("esm.encoder.layer.", index),
            function(x) {
                normalized <- rllm_norm(
                    x, self_norm, kind = "layer", eps = rms_eps,
                    bias = self_norm_bias
                )
                attention <- rllm_op(
                    list(hidden = normalized,
                         padding_mask = inputs$padding_mask),
                    "attention", query = query, key = key, value = value,
                    output = output, heads = n_head,
                    rope = list(type = "neox", base = 10000),
                    mask = list(type = "key_padding"),
                    outputs = list(
                        hidden = hidden_shape, weights = attention_shape
                    )
                )
                joined <- rllm_op(
                    list(residual = x, update = attention$hidden), "add"
                )
                joined <- rllm_residual(joined, function(branch) {
                    branch |>
                        rllm_norm(
                            final_norm, kind = "layer", eps = rms_eps,
                            bias = final_norm_bias
                        ) |>
                        rllm_linear(fc1, fc1_bias) |>
                        rllm_op("gelu") |>
                        rllm_linear(fc2, fc2_bias)
                })
                list(hidden = joined, attention = attention$weights)
            }
        )
        layer <- block(hidden)
        hidden <- layer$hidden |>
            rllm_tap(paste0("representation.", index))
        attentions[[index]] <- layer$attention |>
            rllm_tap(paste0("attention.", index))
    }
    names(attentions) <- paste0("layer.", seq_len(n_layer))

    final_norm <- parameter(
        "emb_layer_norm_after.weight", n_embd, "output_norm"
    )
    final_norm_bias <- parameter(
        "emb_layer_norm_after.bias", n_embd, "output_norm_bias"
    )
    representation <- hidden |>
        rllm_norm(
            final_norm, kind = "layer", eps = rms_eps,
            bias = final_norm_bias
        ) |>
        rllm_tap("representation.final")
    lm_dense <- parameter(
        "lm_head.dense.weight", c(n_embd, n_embd), "lm_head.dense"
    )
    lm_dense_bias <- parameter(
        "lm_head.dense.bias", n_embd, "lm_head.dense_bias"
    )
    lm_norm <- parameter(
        "lm_head.layer_norm.weight", n_embd, "lm_head.norm"
    )
    lm_norm_bias <- parameter(
        "lm_head.layer_norm.bias", n_embd, "lm_head.norm_bias"
    )
    lm_bias <- parameter("lm_head.bias", n_vocab, "lm_head.bias")
    logits <- representation |>
        rllm_linear(lm_dense, lm_dense_bias) |>
        rllm_op("gelu") |>
        rllm_norm(
            lm_norm, kind = "layer", eps = rms_eps, bias = lm_norm_bias
        ) |>
        rllm_op(
            "tied_projection", weight = embedding, bias = lm_bias,
            output_shape = c(
                feature = n_vocab, sequence = n_residue, batch = "batch"
            )
        )

    contact_inputs <- c(list(tokens = inputs$tokens), attentions)
    contacts <- rllm_op(
        contact_inputs, "esm_contact_head",
        regression = parameter(
            "contact_head.regression.weight", n_layer * n_head,
            "contact.regression"
        ),
        bias = parameter(
            "contact_head.regression.bias", 1L, "contact.bias"
        ),
        eos_index = eos_id,
        remove_bos = TRUE, remove_eos = TRUE,
        symmetrize = TRUE, average_product_correction = TRUE,
        output_shape = c(
            row = "n_residue_without_specials",
            column = "n_residue_without_specials", batch = "batch"
        )
    )

    rllm_program(
        list(
            logits = logits, representation = representation,
            contacts = contacts
        ),
        "esm2"
    )
}

.rllm_program_esm2 <- function(metadata, directory, rope_mode) {
    arch <- "esm2"
    key <- function(name, default = .rllm_missing) {
        .rllm_metadata(metadata, arch, name, default)
    }
    scalar <- function(name, positive = TRUE, default = .rllm_missing) {
        .rllm_architecture_scalar(
            key, arch, name, positive = positive, default = default
        )
    }
    if (!is.null(rope_mode)) {
        stop("esm2 uses its architecture-defined NEOX rotation")
    }
    n_layer <- scalar("block_count")
    n_embd <- scalar("embedding_length")
    n_head <- scalar("attention.head_count")
    n_ff <- scalar("feed_forward_length", default = 4L * n_embd)
    if (n_embd %% n_head != 0L) {
        stop("esm2 embedding length must be divisible by its head count")
    }
    rms_eps <- as.numeric(key("attention.layer_norm_epsilon", 1e-5))
    token_dropout <- as.numeric(key("token_dropout.training_ratio", 0.12))
    if (length(rms_eps) != 1L || !is.finite(rms_eps) || rms_eps <= 0 ||
        length(token_dropout) != 1L || !is.finite(token_dropout) ||
        token_dropout < 0 || token_dropout >= 1) {
        stop("invalid esm2 normalization or token-dropout metadata")
    }
    embedding_shape <- .rllm_embedding_shape(
        directory, arch, n_embd, "embed_tokens.weight"
    )
    n_vocab <- embedding_shape[[2L]]
    mask_id <- scalar("mask_token_id", positive = FALSE, default = 32L)
    padding_id <- scalar(
        "padding_token_id", positive = FALSE, default = 1L
    )
    bos_id <- scalar("bos_token_id", positive = FALSE, default = 0L)
    eos_id <- scalar("eos_token_id", positive = FALSE, default = 2L)
    if (any(c(mask_id, padding_id, bos_id, eos_id) >= n_vocab)) {
        stop("esm2 special-token ids must lie inside the vocabulary")
    }

    program <- .rllm_esm2_program(
        n_layer, n_embd, n_head, n_ff, n_vocab, rms_eps,
        mask_id, padding_id, bos_id, eos_id, token_dropout
    )
    list(
        program = program,
        symbols = list(
            n_layer = n_layer, n_embd = n_embd, n_head = n_head,
            n_ff = n_ff, n_vocab = n_vocab, rms_eps = rms_eps,
            mask_id = mask_id, padding_id = padding_id,
            bos_id = bos_id, eos_id = eos_id,
            token_dropout = token_dropout
        )
    )
}

.rllm_execute_esm_projection <- function(specification, x, parameters,
                                         what) {
    if (!is.list(specification)) stop(what, " must be a projection list")
    weight <- .rllm_execute_parameter(
        specification$weight, parameters, paste0(what, " weight")
    )
    if (!is.matrix(weight) || nrow(weight) != nrow(x)) {
        stop(what, " weight does not match the hidden dimension")
    }
    out <- crossprod(weight, x)
    if (!is.null(specification$bias)) {
        bias <- .rllm_execute_parameter(
            specification$bias, parameters, paste0(what, " bias")
        )
        if (length(bias) != nrow(out)) {
            stop(what, " bias does not match the projected dimension")
        }
        out <- sweep(out, 1L, bias, `+`)
    }
    out
}

.rllm_execute_esm_embedding <- function(inputs, attributes, parameters,
                                        context) {
    tokens <- inputs$tokens
    padding <- inputs$padding_mask
    if (is.null(tokens) || is.null(padding)) {
        stop("esm_token_embedding needs tokens and padding_mask")
    }
    token_dim <- dim(tokens)
    if (is.null(token_dim)) token_dim <- c(length(tokens), 1L)
    if (length(token_dim) != 2L || !identical(dim(padding), dim(tokens)) &&
        !(is.null(dim(padding)) && is.null(dim(tokens)))) {
        stop("ESM tokens and padding mask must have sequence-by-batch shape")
    }
    token_values <- as.integer(tokens)
    if (anyNA(token_values) || any(token_values < 0L)) {
        stop("ESM token ids must be non-negative integers")
    }
    padding_values <- as.logical(padding)
    if (anyNA(padding_values)) stop("ESM padding mask must not contain NA")
    weight <- .rllm_execute_parameter(
        attributes$weight, parameters, "ESM token embedding weight"
    )
    if (!is.matrix(weight) || any(token_values >= ncol(weight))) {
        stop("ESM token ids lie outside the embedding vocabulary")
    }
    hidden <- weight[, token_values + 1L, drop = FALSE]
    dropout <- attributes$token_dropout
    if (!is.list(dropout) || !is.numeric(dropout$training_ratio) ||
        length(dropout$training_ratio) != 1L ||
        !is.numeric(dropout$mask_index) || length(dropout$mask_index) != 1L) {
        stop("ESM token dropout has no mask index or training ratio")
    }
    dim(hidden) <- c(nrow(weight), token_dim)
    token_matrix <- matrix(token_values, nrow = token_dim[[1L]])
    padding_matrix <- matrix(padding_values, nrow = token_dim[[1L]])
    for (batch in seq_len(token_dim[[2L]])) {
        masked <- token_matrix[, batch] == as.integer(dropout$mask_index)
        hidden[, masked, batch] <- 0
        hidden[, padding_matrix[, batch], batch] <- 0
        source_length <- sum(!padding_matrix[, batch])
        if (source_length < 1L) stop("ESM input batch contains only padding")
        observed <- sum(masked) / source_length
        if (observed >= 1) stop("ESM input batch contains only mask tokens")
        scale <- (1 - dropout$training_ratio) / (1 - observed)
        hidden[, , batch] <- hidden[, , batch] * scale
    }
    hidden
}

.rllm_execute_esm_rope <- function(x, heads, base) {
    head_dim <- nrow(x) %/% heads
    half <- head_dim %/% 2L
    if (heads * head_dim != nrow(x) || head_dim %% 2L != 0L) {
        stop("ESM rotary attention has an invalid head dimension")
    }
    frequency <- base^(-(2 * (seq_len(half) - 1L)) / head_dim)
    out <- x
    for (head in seq_len(heads)) {
        at <- (head - 1L) * head_dim + seq_len(head_dim)
        first <- at[seq_len(half)]
        second <- at[half + seq_len(half)]
        for (position in seq_len(ncol(x))) {
            angle <- (position - 1L) * frequency
            cosine <- cos(angle)
            sine <- sin(angle)
            a <- x[first, position]
            b <- x[second, position]
            out[first, position] <- a * cosine - b * sine
            out[second, position] <- b * cosine + a * sine
        }
    }
    out
}

.rllm_execute_esm_attention <- function(inputs, attributes, parameters,
                                        context) {
    hidden <- inputs$hidden
    padding <- inputs$padding_mask
    dimensions <- dim(hidden)
    if (is.null(dimensions) || length(dimensions) != 3L ||
        !identical(dim(padding), dimensions[2:3])) {
        stop("ESM attention needs feature-by-sequence-by-batch hidden state")
    }
    heads <- .rllm_count(attributes$heads, "ESM attention heads")
    head_dim <- dimensions[[1L]] %/% heads
    if (heads * head_dim != dimensions[[1L]]) {
        stop("ESM hidden dimension is not divisible by its head count")
    }
    if (!identical(attributes$mask$type, "key_padding") ||
        !identical(attributes$rope$type, "neox")) {
        stop("dense ESM attention requires key padding and NEOX rotation")
    }
    base <- as.numeric(attributes$rope$base)
    if (length(base) != 1L || !is.finite(base) || base <= 0) {
        stop("ESM rotary base must be positive")
    }
    n_token <- dimensions[[2L]]
    n_batch <- dimensions[[3L]]
    output <- array(0, dimensions)
    weights <- array(0, c(heads, n_token, n_token, n_batch))
    padding <- matrix(as.logical(padding), nrow = n_token)

    for (batch in seq_len(n_batch)) {
        x <- matrix(hidden[, , batch], nrow = dimensions[[1L]])
        query <- .rllm_execute_esm_projection(
            attributes$query, x, parameters, "ESM query"
        ) / sqrt(head_dim)
        key <- .rllm_execute_esm_projection(
            attributes$key, x, parameters, "ESM key"
        )
        value <- .rllm_execute_esm_projection(
            attributes$value, x, parameters, "ESM value"
        )
        query <- .rllm_execute_esm_rope(query, heads, base)
        key <- .rllm_execute_esm_rope(key, heads, base)
        mixed <- matrix(0, nrow = dimensions[[1L]], ncol = n_token)

        for (head in seq_len(heads)) {
            at <- (head - 1L) * head_dim + seq_len(head_dim)
            score <- crossprod(key[at, , drop = FALSE],
                               query[at, , drop = FALSE])
            score[padding[, batch], ] <- -Inf
            probability <- matrix(0, n_token, n_token)
            for (position in seq_len(n_token)) {
                column <- score[, position]
                maximum <- max(column)
                if (!is.finite(maximum)) {
                    stop("ESM attention has no unmasked key")
                }
                column <- exp(column - maximum)
                probability[, position] <- column / sum(column)
            }
            mixed[at, ] <- value[at, , drop = FALSE] %*% probability
            weights[head, , , batch] <- t(probability)
        }
        output[, , batch] <- .rllm_execute_esm_projection(
            attributes$output, mixed, parameters, "ESM attention output"
        )
        weights[, padding[, batch], , batch] <- 0
        weights[, , padding[, batch], batch] <- 0
    }
    list(hidden = output, weights = weights)
}

.rllm_execute_tied_projection <- function(inputs, attributes, parameters,
                                          context) {
    hidden <- .rllm_execute_feature_matrix(
        inputs[[1L]], "tied projection input"
    )
    weight <- .rllm_execute_parameter(
        attributes$weight, parameters, "tied projection weight"
    )
    if (!is.matrix(weight) || nrow(weight) != nrow(hidden$value)) {
        stop("tied projection weight has the wrong hidden dimension")
    }
    out <- crossprod(weight, hidden$value)
    if (!is.null(attributes$bias)) {
        bias <- .rllm_execute_parameter(
            attributes$bias, parameters, "tied projection bias"
        )
        if (length(bias) != nrow(out)) {
            stop("tied projection bias has the wrong vocabulary dimension")
        }
        out <- sweep(out, 1L, bias, `+`)
    }
    dimensions <- hidden$dimensions
    if (!is.null(dimensions)) dimensions[[1L]] <- nrow(out)
    .rllm_execute_restore(out, dimensions)
}

.rllm_execute_esm_contact <- function(inputs, attributes, parameters,
                                      context) {
    tokens <- inputs$tokens
    attentions <- inputs[setdiff(names(inputs), "tokens")]
    token_dim <- dim(tokens)
    if (is.null(token_dim)) token_dim <- c(length(tokens), 1L)
    if (length(token_dim) != 2L || !length(attentions)) {
        stop("ESM contact head needs tokens and per-layer attention maps")
    }
    n_token <- token_dim[[1L]]
    n_batch <- token_dim[[2L]]
    tokens <- matrix(as.integer(tokens), nrow = n_token)
    heads <- dim(attentions[[1L]])[[1L]]
    if (is.null(heads) || any(!vapply(attentions, function(x) {
        identical(dim(x), c(heads, n_token, n_token, n_batch))
    }, logical(1)))) {
        stop("ESM contact attention maps have inconsistent dimensions")
    }
    regression <- .rllm_execute_parameter(
        attributes$regression, parameters, "contact regression"
    )
    bias <- .rllm_execute_parameter(
        attributes$bias, parameters, "contact regression bias"
    )
    channels <- length(attentions) * heads
    if (length(regression) != channels || length(bias) != 1L) {
        stop("ESM contact regression has the wrong channel count")
    }
    keep <- seq_len(n_token)
    if (isTRUE(attributes$remove_eos)) keep <- keep[-length(keep)]
    if (isTRUE(attributes$remove_bos)) keep <- keep[-1L]
    contacts <- array(0, c(length(keep), length(keep), n_batch))

    for (batch in seq_len(n_batch)) {
        features <- array(0, c(channels, n_token, n_token))
        channel <- 1L
        for (attention in attentions) {
            for (head in seq_len(heads)) {
                features[channel, , ] <- attention[head, , , batch]
                channel <- channel + 1L
            }
        }
        eos <- tokens[, batch] == as.integer(attributes$eos_index)
        features[, eos, ] <- 0
        features[, , eos] <- 0
        features <- features[, keep, keep, drop = FALSE]
        for (channel in seq_len(channels)) {
            feature <- features[channel, , ]
            if (isTRUE(attributes$symmetrize)) {
                feature <- feature + t(feature)
            }
            if (isTRUE(attributes$average_product_correction)) {
                row <- rowSums(feature)
                column <- colSums(feature)
                total <- sum(feature)
                if (total == 0) stop("ESM contact feature has zero mass")
                feature <- feature - outer(row, column) / total
            }
            features[channel, , ] <- feature
        }
        score <- matrix(bias[[1L]], length(keep), length(keep))
        for (channel in seq_len(channels)) {
            score <- score + regression[[channel]] * features[channel, , ]
        }
        contacts[, , batch] <- stats::plogis(score)
    }
    contacts
}
