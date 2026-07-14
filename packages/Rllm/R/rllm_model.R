#' Load a llama-architecture GGUF model for forward passes
#'
#' Reads the hyperparameters and weights of a llama-architecture 'GGUF' file
#' into an `rllm_model` object usable with [rllm_forward()]. Every weight is a
#' borrowed view over its exact read-only span in the original GGUF mapping.
#' Quantized and floating-point payloads keep their on-disk encoding, and the
#' forward pass points GGML tensors at those bytes without copying them into a
#' second backing file.
#'
#' The loader expects the standard llama tensor names (`token_embd.weight`,
#' `blk.<i>.attn_q.weight`, ..., `output_norm.weight`) and hyperparameter keys
#' (`<arch>.block_count`, `<arch>.embedding_length`, ...). Models with tied
#' embeddings (no `output.weight`) reuse `token_embd.weight` as the output
#' projection.
#'
#' @param path Path to a GGUF file.
#' @param runtime Optional [Rfmalloc::open_fmalloc()] runtime attached to the
#'   borrowed tensor views. It supplies the allocation context for operations
#'   which produce fmalloc results; the weight bytes remain in the GGUF mapping.
#' @param rope_mode RoPE flavour: `0` (normal/interleaved, llama) or `2`
#'   (NEOX-style, e.g. qwen2). Defaults to `0`.
#'
#' @return An object of class `rllm_model` containing its hyperparameters,
#'   borrowed weight payloads, tokenizer metadata, and model-owned backend
#'   contexts created lazily by [rllm_forward()].
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
        nt <- Rgguf::gguf_tensor(ctx, nm, runtime = runtime, as = "view")
        tensors[[nm]] <- list(
            payload = nt,
            type = Rfmalloc::fmalloc_tensor_dtype(nt),
            dims = as.integer(dims)
        )
    }

    structure(list(
        hparams = list(
            n_layer = n_layer, n_embd = n_embd, n_head = n_head,
            n_head_kv = n_head_kv, n_ff = n_ff, n_vocab = n_vocab,
            rms_eps = rms_eps, rope_base = rope_base, rope_dims = rope_dims
        ),
        tensors = tensors,
        rope_mode = as.integer(rope_mode),
        arch = arch,
        # Tokenizer material for the ids <-> bytes edge codecs (rllm_encode/
        # rllm_decode); NULL when the file carries none (synthetic models).
        tok_model = md[["tokenizer.ggml.model"]],
        vocab = md[["tokenizer.ggml.tokens"]],
        merges = md[["tokenizer.ggml.merges"]],
        eos_id = md[["tokenizer.ggml.eos_token_id"]],
        .contexts = new.env(parent = emptyenv())
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

#' Create a KV cache for incremental decoding
#'
#' Allocates the per-layer key/value cache slabs an incremental
#' [rllm_forward()] writes into and attends over. Each slab is a raw vector of
#' `n_ctx * (n_embd / n_head) * n_head_kv` f32 values - plain R memory by
#' default, or \pkg{Rfmalloc}-backed (file-backed, memory-mapped) when a
#' `runtime` is given, which makes the cache a disk citizen: it survives in
#' the runtime's file and its pages are evictable like any other fmalloc
#' payload. (A quantized cache codec in the TurboQuant/PolarQuant vein can
#' later replace the f32 slabs without touching the graph.)
#'
#' The returned object is an environment, so [rllm_forward()] can advance its
#' `n_past` by reference.
#'
#' @param model An `rllm_model` from [rllm_gguf_model()].
#' @param n_ctx Maximum number of positions the cache can hold.
#' @param runtime Optional [Rfmalloc::open_fmalloc()] runtime for the slabs.
#'
#' @return An environment of class `rllm_kv_cache` with fields `k`, `v`
#'   (per-layer lists of raw vectors), `n_ctx`, and `n_past`.
#' @seealso [rllm_forward()], [rllm_generate()]
#' @export
rllm_kv_cache <- function(model, n_ctx = 512L, runtime = NULL) {
    if (!inherits(model, "rllm_model")) {
        stop("model must be an rllm_model from rllm_gguf_model()")
    }
    hp <- model$hparams
    n_ctx <- as.integer(n_ctx)
    if (n_ctx < 1L) stop("n_ctx must be positive")
    slab_bytes <- as.double(n_ctx) * (hp$n_embd / hp$n_head) * hp$n_head_kv * 4

    new_slab <- function() {
        if (is.null(runtime)) {
            raw(slab_bytes)
        } else {
            Rfmalloc::create_fmalloc_vector("raw", length = slab_bytes,
                                            runtime = runtime,
                                            zero_initialize = TRUE)
        }
    }
    cache <- new.env(parent = emptyenv())
    cache$k <- replicate(hp$n_layer, new_slab(), simplify = FALSE)
    cache$v <- replicate(hp$n_layer, new_slab(), simplify = FALSE)
    cache$n_ctx <- n_ctx
    cache$n_past <- 0L
    class(cache) <- "rllm_kv_cache"
    cache
}

#' @export
print.rllm_kv_cache <- function(x, ...) {
    cat(sprintf("<rllm_kv_cache: %d/%d positions used, %d layers, %s slabs>\n",
                x$n_past, x$n_ctx, length(x$k),
                if (Rfmalloc::is_fmalloc_vector(x$k[[1L]])) "fmalloc" else "R"))
    invisible(x)
}

#' Run a transformer forward pass and return the logits
#'
#' Assembles the GGML compute graph for a llama-architecture forward pass
#' (RMSNorm, RoPE, causal self-attention, SwiGLU feed-forward) over the
#' model's memory-mapped weights and computes it on a chosen GGML backend.
#' Quantized weights are contracted natively in their encoded form - they are
#' never decoded to double. The CPU backend borrows the mapped bytes directly.
#' On its first use, the CUDA backend creates a model-owned context and uploads
#' the codec-native weights once. Later passes reuse those resident weights;
#' mutable inputs, cache slabs and logits move through Rggml's transfer API.
#'
#' Without a `cache`, the graph attends over the whole token batch with a
#' causal mask (prompt scoring). With a [rllm_kv_cache()], the pass appends
#' the new tokens' keys/values to the cache and attends over everything
#' cached so far, advancing `cache$n_past` - the incremental-decoding path:
#' prefill once with the prompt, then feed one token at a time.
#'
#' @param model An `rllm_model` from [rllm_gguf_model()].
#' @param tokens Integer vector of 0-based token ids (as in the GGUF vocab).
#' @param cache Optional [rllm_kv_cache()] for incremental decoding.
#' @param backend Compute backend. `"cpu"` is the zero-copy mmap path;
#'   `"cuda"` requires Rggml installed with `--with-cuda` and a visible NVIDIA
#'   device.
#'
#' @return A numeric matrix of logits, dim `c(n_vocab, length(tokens))`:
#'   column `i` scores the token following position `i`.
#' @export
rllm_forward <- function(model, tokens, cache = NULL,
                         backend = c("cpu", "cuda")) {
    if (!inherits(model, "rllm_model")) {
        stop("model must be an rllm_model from rllm_gguf_model()")
    }
    tokens <- as.integer(tokens)
    if (length(tokens) < 1L || anyNA(tokens)) {
        stop("tokens must be a non-empty vector of 0-based token ids")
    }
    backend <- match.arg(backend)
    backend_code <- c(cpu = 0L, cuda = 3L)[[backend]]
    backend_context <- NULL
    if (backend == "cuda") {
        if (!is.environment(model$.contexts)) {
            stop("model has no backend-context environment")
        }
        backend_context <- model$.contexts$cuda
        if (is.null(backend_context)) {
            backend_context <- .Call(
                "RC_rllm_cuda_model_context", model$tensors,
                PACKAGE = "Rllm"
            )
            model$.contexts$cuda <- backend_context
        }
    }
    if (is.null(cache)) {
        return(.Call("RC_rllm_llama_forward", model$hparams, model$tensors,
                     tokens, model$rope_mode, NULL, NULL, 0L, backend_code,
                     backend_context, PACKAGE = "Rllm"))
    }
    if (!inherits(cache, "rllm_kv_cache")) {
        stop("cache must be an rllm_kv_cache from rllm_kv_cache()")
    }
    out <- .Call("RC_rllm_llama_forward", model$hparams, model$tensors,
                 tokens, model$rope_mode, cache$k, cache$v, cache$n_past,
                 backend_code, backend_context, PACKAGE = "Rllm")
    cache$n_past <- cache$n_past + length(tokens)
    out
}
