library(tinytest)
library(Rllm)

# Structural record of prism-ml/Ternary-Bonsai-27B-Q2_g64.gguf. No weight
# payload is fabricated: this pins the real file's metadata, tensor roles and
# heterogeneous layer schedule while the optional path below parses the file
# itself when it is available.
metadata <- list(
    general.architecture = "qwen35",
    qwen35.block_count = 64L,
    qwen35.context_length = 262144L,
    qwen35.embedding_length = 5120L,
    qwen35.feed_forward_length = 17408L,
    qwen35.attention.head_count = 24L,
    qwen35.attention.head_count_kv = 4L,
    qwen35.rope.dimension_sections = c(11L, 11L, 10L, 0L),
    qwen35.rope.freq_base = 1e7,
    qwen35.attention.layer_norm_rms_epsilon = 1e-6,
    qwen35.attention.key_length = 256L,
    qwen35.attention.value_length = 256L,
    qwen35.ssm.conv_kernel = 4L,
    qwen35.ssm.state_size = 128L,
    qwen35.ssm.group_count = 16L,
    qwen35.ssm.time_step_rank = 48L,
    qwen35.ssm.inner_size = 6144L,
    qwen35.full_attention_interval = 4L,
    qwen35.rope.dimension_count = 64L
)

tensor_names <- character()
tensor_dims <- list()
add_tensor <- function(name, dims) {
    tensor_names[[length(tensor_names) + 1L]] <<- name
    tensor_dims[[length(tensor_dims) + 1L]] <<- as.integer(dims)
}

add_tensor("token_embd.weight", c(5120L, 248320L))
add_tensor("output_norm.weight", 5120L)
add_tensor("output.weight", c(5120L, 248320L))
for (il in 0:63) {
    prefix <- paste0("blk.", il, ".")
    add <- function(suffix, dims) add_tensor(paste0(prefix, suffix), dims)
    add("attn_norm.weight", 5120L)
    add("post_attention_norm.weight", 5120L)
    add("ffn_gate.weight", c(5120L, 17408L))
    add("ffn_up.weight", c(5120L, 17408L))
    add("ffn_down.weight", c(17408L, 5120L))
    if ((il + 1L) %% 4L != 0L) {
        add("attn_qkv.weight", c(5120L, 10240L))
        add("attn_gate.weight", c(5120L, 6144L))
        add("ssm_conv1d.weight", c(4L, 10240L))
        add("ssm_dt.bias", 48L)
        add("ssm_a", 48L)
        add("ssm_beta.weight", c(5120L, 48L))
        add("ssm_alpha.weight", c(5120L, 48L))
        add("ssm_norm.weight", 128L)
        add("ssm_out.weight", c(6144L, 5120L))
    } else {
        add("attn_q.weight", c(5120L, 12288L))
        add("attn_k.weight", c(5120L, 1024L))
        add("attn_v.weight", c(5120L, 1024L))
        add("attn_output.weight", c(6144L, 5120L))
        add("attn_q_norm.weight", 256L)
        add("attn_k_norm.weight", 256L)
    }
}
directory <- data.frame(name = tensor_names, stringsAsFactors = FALSE)
directory$dims <- I(tensor_dims)

plan <- Rllm:::.rllm_plan_from_gguf(metadata, directory)
operators <- vapply(plan$layers, function(layer) layer$operator$op,
                    character(1))
states <- vapply(plan$layers, function(layer) layer$state$op, character(1))

expect_equal(plan$architecture, "qwen35")
expect_equal(length(plan$tensors), 851L)
expect_equal(length(plan$program$parameters), 851L)
expect_equal(length(plan$program$nodes), 388L)
expect_equal(as.integer(table(operators)[c("gated_delta_net", "gated_attention")]),
             c(48L, 16L))
expect_equal(as.integer(table(states)[c("gated_delta", "kv")]), c(48L, 16L))
expect_equal(which(operators == "gated_attention") - 1L, seq(3L, 63L, by = 4L))
expect_equal(plan$layers[[1L]]$state$matrix,
             c(row = 128L, column = 128L, head = 48L))
expect_equal(plan$layers[[1L]]$state$convolution,
             c(width = 10240L, history = 3L))
expect_equal(plan$layers[[4L]]$operator$query_gate_layout,
             "head_interleaved")
expect_equal(plan$layers[[4L]]$operator$rope$sections,
             c(11L, 11L, 10L, 0L))
expect_identical(unserialize(serialize(plan$program, NULL)), plan$program)

bad <- directory
at <- match("blk.3.attn_q.weight", bad$name)
bad$dims[[at]] <- c(5120L, 6144L)
expect_error(Rllm:::.rllm_plan_from_gguf(metadata, bad),
             "attention.query_gate.*expected.*12288")

real_path <- Sys.getenv("RLLM_QWEN35_GGUF")
if (nzchar(real_path) && file.exists(real_path)) {
    real_plan <- rllm_plan(real_path)
    expect_equal(real_plan$symbols, plan$symbols)
    expect_equal(names(real_plan$tensors), names(plan$tensors))
    expect_equal(vapply(real_plan$layers, function(layer) layer$operator$op,
                        character(1)), operators)
}

message("Qwen3.5 hybrid plan tests completed")
