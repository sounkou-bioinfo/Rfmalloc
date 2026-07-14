args <- commandArgs(trailingOnly = TRUE)
path <- if (length(args)) {
    args[[1L]]
} else {
    file.path("inst", "extdata", "tiny-byte-model.gguf")
}

# GPT-2's printable-codepoint representation, indexed here by byte value so
# token id b decodes to byte b. There are deliberately no merges: this fixture
# tests the byte boundary and the complete graph, not tokenizer quality.
keep <- c(33:126, 161:172, 174:255)
bytes <- keep
codepoints <- keep
next_codepoint <- 256L
for (byte in 0:255) {
    if (!(byte %in% keep)) {
        bytes <- c(bytes, byte)
        codepoints <- c(codepoints, next_codepoint)
        next_codepoint <- next_codepoint + 1L
    }
}
b2c <- integer(256L)
b2c[bytes + 1L] <- codepoints
vocab <- vapply(b2c, intToUtf8, character(1))

hp <- list(
    n_layer = 1L,
    n_embd = 16L,
    n_head = 4L,
    n_head_kv = 2L,
    n_ff = 24L,
    n_vocab = 256L
)
kv_dim <- hp$n_embd / hp$n_head * hp$n_head_kv
zero <- function(nrow, ncol) matrix(0, nrow, ncol)

# Every token embeds to the same positive vector. Transformer updates are zero,
# and only the output column for byte 0x21 ('!') is nonzero. Greedy generation
# is deterministic while still traversing embedding, attention, feed-forward,
# KV-cache, output projection, and byte decoding.
tensors <- list(
    "token_embd.weight" = matrix(1, hp$n_embd, hp$n_vocab),
    "output_norm.weight" = rep(1, hp$n_embd),
    "output.weight" = {
        x <- zero(hp$n_embd, hp$n_vocab)
        x[, 0x21L + 1L] <- 1
        x
    },
    "blk.0.attn_norm.weight" = rep(1, hp$n_embd),
    "blk.0.attn_q.weight" = zero(hp$n_embd, hp$n_embd),
    "blk.0.attn_k.weight" = zero(hp$n_embd, kv_dim),
    "blk.0.attn_v.weight" = zero(hp$n_embd, kv_dim),
    "blk.0.attn_output.weight" = zero(hp$n_embd, hp$n_embd),
    "blk.0.ffn_norm.weight" = rep(1, hp$n_embd),
    "blk.0.ffn_gate.weight" = zero(hp$n_embd, hp$n_ff),
    "blk.0.ffn_up.weight" = zero(hp$n_embd, hp$n_ff),
    "blk.0.ffn_down.weight" = zero(hp$n_ff, hp$n_embd)
)

dir.create(dirname(path), recursive = TRUE, showWarnings = FALSE)
Rgguf::gguf_write_tensors(path, tensors, metadata = list(
    "general.architecture" = "llama",
    "llama.block_count" = hp$n_layer,
    "llama.embedding_length" = hp$n_embd,
    "llama.attention.head_count" = hp$n_head,
    "llama.attention.head_count_kv" = hp$n_head_kv,
    "llama.feed_forward_length" = hp$n_ff,
    "llama.attention.layer_norm_rms_epsilon" = 1e-5,
    "llama.rope.freq_base" = 10000,
    "llama.rope.dimension_count" = hp$n_embd / hp$n_head,
    "tokenizer.ggml.model" = "gpt2",
    "tokenizer.ggml.tokens" = vocab,
    "tokenizer.ggml.merges" = character()
))

metadata <- Rgguf::gguf_metadata(path)
stopifnot(
    identical(metadata[["tokenizer.ggml.tokens"]], vocab),
    identical(metadata[["tokenizer.ggml.merges"]], character())
)
message(normalizePath(path))
