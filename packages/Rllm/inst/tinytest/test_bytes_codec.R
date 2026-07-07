library(tinytest)
library(Rllm)

# The ids <-> bytes edge codecs (the bytes boundary: model I/O is raw bytes,
# text is the caller's interpretation). Unit-tested against hand-built
# byte-level BPE vocabularies - the exact GPT-2 byte<->unicode alphabet is the
# subtle part: printable latin bytes are their own codepoints, everything else
# (including space, 0x20) is remapped to 256+n, so " " lives in vocab strings
# as "Ġ" (the familiar 'G-with-dot' prefix).

fake_model <- function(vocab, merges) {
    structure(list(vocab = vocab, merges = merges, tok_model = "gpt2"),
              class = "rllm_model")
}

## exact byte-map checks: space (0x20) <-> U+0120, 'a' (0x61) <-> 'a'
mdl <- fake_model(c("a", "Ġ", "Ġa"), c("Ġ a"))
expect_equal(rllm_encode(mdl, charToRaw(" a")), 2L)          # one merged token
expect_equal(rllm_decode(mdl, 2L), charToRaw(" a"))          # back to bytes
expect_equal(rllm_decode(mdl, c(1L, 0L)), charToRaw(" a"))   # unmerged pieces

## BPE merge order: lowest rank first, all occurrences merged
mdl <- fake_model(c("a", "b", "ab", "abab"), c("a b", "ab ab"))
expect_equal(rllm_encode(mdl, charToRaw("abab")), 3L)        # a b a b -> ab ab -> abab
expect_equal(rllm_encode(mdl, charToRaw("aba")), c(2L, 0L))  # ab + a
expect_equal(rllm_decode(mdl, c(2L, 0L)), charToRaw("aba"))

## roundtrip always holds (merges only concatenate)
expect_equal(rllm_decode(mdl, rllm_encode(mdl, charToRaw("abbaab"))),
             charToRaw("abbaab"))

## string convenience on encode; bytes stay the contract on decode
expect_equal(rllm_encode(mdl, "abab"), 3L)
expect_true(is.raw(rllm_decode(mdl, 3L)))

## models without tokenizer metadata refuse clearly
bare <- structure(list(vocab = NULL), class = "rllm_model")
expect_error(rllm_encode(bare, charToRaw("x")), "no tokenizer metadata")

message("bytes-codec tests completed")
