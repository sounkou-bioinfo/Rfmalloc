# The bytes boundary: model I/O is token ids (integer) and raw bytes - never
# character vectors in the core. The tokenizer is an *edge codec* between ids
# and bytes, built entirely from GGUF metadata (tokenizer.ggml.tokens/merges),
# so the same pipeline serves byte-level, vision, or pure-codec models later;
# rawToChar() is the caller's interpretation, not the API's.

# GPT-2 byte <-> unicode alphabet. Byte-level BPE vocabularies store each
# byte as a printable codepoint: printable latin bytes map to themselves,
# the rest are remapped to 256, 257, ... in byte order.
.rllm_byte_maps <- function() {
    keep <- c(33:126, 161:172, 174:255)
    bs <- keep
    cs <- keep
    n <- 0L
    for (b in 0:255) {
        if (!(b %in% keep)) {
            bs <- c(bs, b)
            cs <- c(cs, 256L + n)
            n <- n + 1L
        }
    }
    b2c <- integer(256L)          # byte value (0-based) + 1 -> codepoint
    b2c[bs + 1L] <- cs
    c2b <- rep(NA_integer_, max(cs) + 1L)  # codepoint + 1 -> byte value
    c2b[cs + 1L] <- bs
    list(b2c = b2c, c2b = c2b)
}
.rllm_maps <- NULL
.rllm_get_maps <- function() {
    if (is.null(.rllm_maps)) {
        utils::assignInMyNamespace(".rllm_maps", .rllm_byte_maps())
    }
    .rllm_maps
}

.rllm_check_tokenizer <- function(model) {
    if (is.null(model$vocab)) {
        stop("model carries no tokenizer metadata (tokenizer.ggml.tokens); ",
             "feed token ids directly")
    }
    if (!identical(model$tok_model, "gpt2")) {
        stop("only byte-level BPE ('gpt2') tokenizers are supported; ",
             "this model uses '", model$tok_model %||% "?", "'")
    }
}

`%||%` <- function(a, b) if (is.null(a)) b else a

#' Convert between token ids and raw bytes
#'
#' The ids <-> bytes edge codecs of the bytes-boundary API, built from the
#' model's own GGUF tokenizer metadata (byte-level BPE, `tokenizer.ggml.model
#' == "gpt2"`). `rllm_decode()` maps ids to the exact bytes they stand for.
#' `rllm_encode()` byte-pair-encodes bytes into ids using the file's merge
#' ranks; it applies the merges without GPT-2's regex pre-tokenizer, so a
#' tokenization may occasionally differ from llama.cpp's canonical split -
#' every output is still a valid encoding of the input
#' (`rllm_decode(rllm_encode(x))` is always `x`).
#'
#' @param model An `rllm_model` whose GGUF carries a byte-level BPE tokenizer.
#' @param ids Integer vector of 0-based token ids.
#' @param x A raw vector (or a single string, converted with `charToRaw()`).
#'
#' @return `rllm_decode()`: a raw vector. `rllm_encode()`: an integer vector
#'   of 0-based token ids.
#' @export
rllm_decode <- function(model, ids) {
    .rllm_check_tokenizer(model)
    ids <- as.integer(ids)
    maps <- .rllm_get_maps()
    pieces <- lapply(model$vocab[ids + 1L], function(tok) {
        cps <- utf8ToInt(tok)
        b <- maps$c2b[cps + 1L]
        if (anyNA(b)) {
            # Codepoints outside the byte alphabet (added special tokens like
            # <|endoftext|>): pass their literal UTF-8 bytes through.
            charToRaw(tok)
        } else {
            as.raw(b)
        }
    })
    do.call(c, pieces)
}

#' @rdname rllm_decode
#' @export
rllm_encode <- function(model, x) {
    .rllm_check_tokenizer(model)
    if (is.character(x) && length(x) == 1L) x <- charToRaw(x)
    if (!is.raw(x)) stop("x must be a raw vector (or a single string)")
    if (length(x) == 0L) return(integer(0))
    if (is.null(model$merges)) stop("model carries no tokenizer.ggml.merges")
    maps <- .rllm_get_maps()

    # bytes -> alphabet symbols, then greedy lowest-rank BPE merging
    sym <- vapply(as.integer(x), function(b) intToUtf8(maps$b2c[b + 1L]),
                  character(1))
    while (length(sym) > 1L) {
        pairs <- paste(sym[-length(sym)], sym[-1L])
        ranks <- match(pairs, model$merges)
        if (all(is.na(ranks))) break
        best <- pairs[which.min(ranks)]
        out <- character(0)
        i <- 1L
        while (i <= length(sym)) {
            if (i < length(sym) && paste(sym[i], sym[i + 1L]) == best) {
                out <- c(out, paste0(sym[i], sym[i + 1L]))
                i <- i + 2L
            } else {
                out <- c(out, sym[i])
                i <- i + 1L
            }
        }
        sym <- out
    }
    ids <- match(sym, model$vocab) - 1L
    if (anyNA(ids)) {
        stop("BPE produced symbols missing from the vocabulary (byte ",
             "coverage incomplete?): ", paste(sym[is.na(ids)], collapse = " "))
    }
    ids
}

# Pick the next 0-based token id from a length-n_vocab logits vector.
# temperature <= 0 is greedy (argmax); otherwise temperature-scaled softmax
# sampling, optionally restricted to the top_k highest-logit tokens and/or the
# smallest top_p nucleus of probability mass.
.rllm_sample_next <- function(logits, temperature, top_k, top_p) {
    if (temperature <= 0) return(which.max(logits) - 1L)

    z <- logits / temperature
    if (top_k > 0L && top_k < length(z)) {
        keep <- order(z, decreasing = TRUE)[seq_len(top_k)]
        masked <- rep(-Inf, length(z))
        masked[keep] <- z[keep]
        z <- masked
    }
    p <- exp(z - max(z)); p <- p / sum(p)      # stable softmax
    if (top_p < 1) {
        ord <- order(p, decreasing = TRUE)
        cum <- cumsum(p[ord])
        n_keep <- which(cum >= top_p)[1L]       # smallest nucleus reaching top_p
        drop <- ord[-seq_len(n_keep)]
        p[drop] <- 0; p <- p / sum(p)
    }
    sample.int(length(p), 1L, prob = p) - 1L
}

#' Generate tokens with a KV cache (greedy or sampled)
#'
#' The bytes-in/bytes-out generation entry point: prefills a KV cache with the
#' prompt (ids or raw bytes), then decodes one token per step - each step is a
#' single-token [rllm_forward()] over the cache, not a full re-forward. Stops
#' after `n_new` tokens or at the model's EOS token. Decoding is greedy by
#' default (`temperature = 0`); set a positive temperature for
#' temperature-scaled sampling, optionally narrowed by `top_k` and/or `top_p`.
#'
#' @param model An `rllm_model` from [rllm_gguf_model()].
#' @param prompt Integer vector of 0-based token ids, or a raw vector
#'   (encoded with [rllm_encode()]; a single string is converted via
#'   `charToRaw()` as a convenience).
#' @param n_new Maximum number of tokens to generate.
#' @param temperature Sampling temperature. `0` (default) is greedy/argmax;
#'   larger values flatten the distribution (more diverse).
#' @param top_k Keep only the `top_k` highest-logit tokens before sampling
#'   (`0`, default, disables the cutoff). Ignored when greedy.
#' @param top_p Nucleus sampling: keep the smallest set of tokens whose
#'   probabilities sum to at least `top_p` (`1`, default, disables it).
#'   Ignored when greedy.
#' @param seed Optional integer seed, for reproducible sampling.
#' @param cache Optional [rllm_kv_cache()] to continue from; by default a
#'   fresh cache sized `length(prompt) + n_new` is created.
#' @param runtime Optional [Rfmalloc::open_fmalloc()] runtime for the cache
#'   slabs (file-backed cache).
#'
#' @return A list with `ids` (prompt + generated, 0-based), `new_ids` (the
#'   generated tokens), and `raw` (the generated tokens decoded to bytes, or
#'   `NULL` when the model has no tokenizer metadata).
#' @export
rllm_generate <- function(model, prompt, n_new = 32L, temperature = 0,
                          top_k = 0L, top_p = 1, seed = NULL, cache = NULL,
                          runtime = NULL) {
    if (!inherits(model, "rllm_model")) {
        stop("model must be an rllm_model from rllm_gguf_model()")
    }
    ids <- if (is.raw(prompt) || is.character(prompt)) {
        rllm_encode(model, prompt)
    } else {
        as.integer(prompt)
    }
    if (length(ids) < 1L || anyNA(ids)) stop("empty or invalid prompt")
    n_new <- as.integer(n_new)
    top_k <- as.integer(top_k)
    if (!is.null(seed)) set.seed(as.integer(seed))

    if (is.null(cache)) {
        cache <- rllm_kv_cache(model, n_ctx = length(ids) + n_new,
                               runtime = runtime)
    }
    eos <- if (!is.null(model$eos_id)) as.integer(model$eos_id) else NA_integer_

    logits <- rllm_forward(model, ids, cache)          # prefill
    new_ids <- integer(0)
    for (step in seq_len(n_new)) {
        nxt <- .rllm_sample_next(logits[, ncol(logits)], temperature, top_k, top_p)
        new_ids <- c(new_ids, nxt)
        if (!is.na(eos) && nxt == eos) break
        if (step < n_new) {
            logits <- rllm_forward(model, nxt, cache)  # one token per step
        }
    }
    list(
        ids = c(ids, new_ids),
        new_ids = new_ids,
        raw = if (!is.null(model$vocab) && identical(model$tok_model, "gpt2")) {
            rllm_decode(model, new_ids)
        }
    )
}