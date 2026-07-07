#' Load a llama-architecture GGUF model for forward passes
#'
#' Reads the hyperparameters and weights of a llama-architecture 'GGUF' file
#' into an `rllm_model` object usable with [rllm_forward()]. 2-d weight
#' tensors are imported with `Rgguf::gguf_tensor(as = "native")`: their
#' payloads keep the GGUF storage density (still `q4_k`/`f32`/... encoded) in
#' \pkg{Rfmalloc}-backed, memory-mapped storage, and the forward pass points
#' GGML tensors at them zero-copy. 1-d norm weights are small and are staged
#' as packed `f32` buffers.
#'
#' The loader expects the standard llama tensor names (`token_embd.weight`,
#' `blk.<i>.attn_q.weight`, ..., `output_norm.weight`) and hyperparameter keys
#' (`<arch>.block_count`, `<arch>.embedding_length`, ...). Models with tied
#' embeddings (no `output.weight`) reuse `token_embd.weight` as the output
#' projection.
#'
#' @param path Path to a GGUF file.
#' @param runtime Optional [Rfmalloc::open_fmalloc()] runtime for the weight
#'   payloads; `NULL` uses Rfmalloc's default runtime.
#' @param rope_mode RoPE flavour: `0` (normal/interleaved, llama) or `2`
#'   (NEOX-style, e.g. qwen2). Defaults to `0`.
#'
#' @return An object of class `rllm_model`: a list with `hparams` (named
#'   numeric list), `tensors` (named list of weight payloads), and
#'   `rope_mode`.
#' @seealso [rllm_forward()]
#' @export
rllm_gguf_model <- function(path, runtime = NULL, rope_mode = 0L) {
    ctx <- Rgguf::gguf_open(path)
    md <- Rgguf::gguf_metadata(ctx)
    tt <- Rgguf::gguf_tensors(ctx)

    arch <- md[["general.architecture"]]
    if (is.null(arch)) {
        stop("GGUF file has no 'general.architecture' metadata key")
    }
    h <- function(key, default = NULL) {
        v <- md[[paste0(arch, ".", key)]]
        if (is.null(v)) {
            if (is.null(default)) {
                stop("missing GGUF hyperparameter '", paste0(arch, ".", key), "'")
            }
            return(default)
        }
        as.numeric(v)
    }

    n_layer <- h("block_count")
    n_embd  <- h("embedding_length")
    n_head  <- h("attention.head_count")
    n_head_kv <- h("attention.head_count_kv", n_head)
    n_ff    <- h("feed_forward_length")
    rms_eps <- h("attention.layer_norm_rms_epsilon", 1e-5)
    rope_base <- h("rope.freq_base", 10000)
    head_dim <- n_embd / n_head
    rope_dims <- h("rope.dimension_count", head_dim)

    emb_row <- tt[tt$name == "token_embd.weight", ]
    if (nrow(emb_row) != 1L) stop("GGUF file has no 'token_embd.weight' tensor")
    n_vocab <- emb_row$dims[[1L]][2L]

    needed <- c(
        "token_embd.weight", "output_norm.weight",
        if ("output.weight" %in% tt$name) "output.weight",
        as.vector(t(outer(
            paste0("blk.", seq_len(n_layer) - 1L, "."),
            c("attn_norm.weight", "attn_q.weight", "attn_k.weight",
              "attn_v.weight", "attn_output.weight", "ffn_norm.weight",
              "ffn_gate.weight", "ffn_up.weight", "ffn_down.weight"),
            paste0)))
    )
    missing <- setdiff(needed, tt$name)
    if (length(missing) > 0L) {
        stop("GGUF file is missing tensors: ", paste(missing, collapse = ", "))
    }

    tensors <- vector("list", length(needed))
    names(tensors) <- needed
    for (nm in needed) {
        row <- tt[tt$name == nm, ]
        dims <- row$dims[[1L]]
        if (row$n_dims == 2L) {
            # Native import: payload keeps its GGUF encoding, fmalloc-backed.
            nt <- Rgguf::gguf_tensor(ctx, nm, runtime = runtime, as = "native")
            tensors[[nm]] <- list(
                payload = unclass(nt),
                type = Rfmalloc::fmalloc_tensor_dtype(nt),
                dims = as.integer(dims)
            )
        } else {
            # 1-d norm weights: small; stage as a packed f32 buffer.
            v <- Rgguf::gguf_tensor(ctx, nm, runtime = runtime, as = "numeric")
            tensors[[nm]] <- list(
                payload = .Call("RC_rllm_as_f32", as.double(v), PACKAGE = "Rllm"),
                type = "f32",
                dims = as.integer(dims)
            )
        }
    }

    structure(list(
        hparams = list(
            n_layer = n_layer, n_embd = n_embd, n_head = n_head,
            n_head_kv = n_head_kv, n_ff = n_ff, n_vocab = n_vocab,
            rms_eps = rms_eps, rope_base = rope_base, rope_dims = rope_dims
        ),
        tensors = tensors,
        rope_mode = as.integer(rope_mode),
        arch = arch
    ), class = "rllm_model")
}

#' @export
print.rllm_model <- function(x, ...) {
    hp <- x$hparams
    types <- vapply(x$tensors, function(t) t$type, character(1))
    cat(sprintf(
        "<rllm_model %s: %d layers, n_embd %d, %d/%d heads, n_ff %d, vocab %d; %d tensors (%s)>\n",
        x$arch, hp$n_layer, hp$n_embd, hp$n_head, hp$n_head_kv, hp$n_ff,
        hp$n_vocab, length(x$tensors),
        paste(names(sort(table(types), decreasing = TRUE)), collapse = "/")
    ))
    invisible(x)
}

#' Run a transformer forward pass and return the logits
#'
#' Assembles the GGML compute graph for a llama-architecture forward pass
#' (RMSNorm, RoPE, causal self-attention, SwiGLU feed-forward) over the
#' model's memory-mapped weights and computes it on the GGML CPU backend.
#' Quantized weights are contracted natively through the SIMD-dispatched
#' quantized kernels - they are never decoded to double. There is no KV cache
#' yet: the graph attends over the whole token batch with a causal mask, so
#' this is a prompt-scoring (not incremental-generation) entry point.
#'
#' @param model An `rllm_model` from [rllm_gguf_model()].
#' @param tokens Integer vector of 0-based token ids (as in the GGUF vocab).
#'
#' @return A numeric matrix of logits, dim `c(n_vocab, length(tokens))`:
#'   column `i` scores the token following position `i`.
#' @export
rllm_forward <- function(model, tokens) {
    if (!inherits(model, "rllm_model")) {
        stop("model must be an rllm_model from rllm_gguf_model()")
    }
    tokens <- as.integer(tokens)
    if (length(tokens) < 1L || anyNA(tokens)) {
        stop("tokens must be a non-empty vector of 0-based token ids")
    }
    .Call("RC_rllm_llama_forward", model$hparams, model$tensors, tokens,
          model$rope_mode, PACKAGE = "Rllm")
}
