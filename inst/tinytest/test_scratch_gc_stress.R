library(tinytest)
library(Rfmalloc)

message("Testing scratch-mode GC integration for recursive VECSXP structures and strings...")

walk_list_chain <- function(node, depth) {
    cur <- node
    for (i in seq_len(depth)) {
        cur <- cur[[1]]
    }
    cur
}

(function() {
    scratch_file <- tempfile(fileext = ".bin")
    on.exit({
        cleanup_fmalloc(rt)
        unlink(scratch_file)
    }, add = TRUE)

    rt <- open_fmalloc(scratch_file, mode = "scratch", size_gb = 0.05)
    depth <- 64L

    root <- create_fmalloc_vector("list", 1, runtime = rt)
    current <- root
    for (i in seq_len(depth - 1L)) {
        child <- create_fmalloc_vector("list", 1, runtime = rt)
        current[[1]] <- child
        current <- child
    }

    leaf <- create_fmalloc_vector("integer", 1, runtime = rt)
    leaf[] <- 11L
    current[[1]] <- leaf

    leaf_ref <- walk_list_chain(root, depth)
    expect_true(is.integer(leaf_ref))
    expect_equal(leaf_ref[1], 11L)

    for (step in 1:500L) {
        target <- walk_list_chain(root, depth)
        target[1] <- as.integer(step)
        expect_equal(target[1], as.integer(step))
        if ((step %% 50L) == 0L) {
            gc()
            expect_equal(walk_list_chain(root, depth)[1], as.integer(step))
        }
    }

    gc()
    final_leaf <- walk_list_chain(root, depth)
    expect_equal(final_leaf[1], 500L)
    message("  Deep recursive list chain in scratch mode passed")
})()

(function() {
    scratch_file <- tempfile(fileext = ".bin")
    on.exit({
        cleanup_fmalloc(rt)
        unlink(scratch_file)
    }, add = TRUE)

    rt <- open_fmalloc(scratch_file, mode = "scratch", size_gb = 0.05)
    parent <- create_fmalloc_vector("list", 1, runtime = rt)
    chars <- create_fmalloc_vector("character", 1, runtime = rt)
    chars[] <- "seed"
    parent[[1]] <- chars

    for (step in 1:1000L) {
        chars[1] <- paste0("token-", step)
        expect_equal(chars[1], paste0("token-", step))
        if ((step %% 100L) == 0L) {
            gc()
            expect_equal(parent[[1]][1], paste0("token-", step))
        }
    }

    expect_equal(parent[[1]][1], "token-1000")
    message("  String churn through list-owned child in scratch mode passed")
})()

(function() {
    scratch_file <- tempfile(fileext = ".bin")
    on.exit({
        cleanup_fmalloc(rt)
        unlink(scratch_file)
    }, add = TRUE)

    rt <- open_fmalloc(scratch_file, mode = "scratch", size_gb = 0.05)
    left <- create_fmalloc_vector("list", 3, runtime = rt)
    right <- create_fmalloc_vector("list", 3, runtime = rt)

    shared_child <- create_fmalloc_vector("integer", 1, runtime = rt)
    shared_child[] <- 7L
    shared_str <- create_fmalloc_vector("character", 1, runtime = rt)
    shared_str[] <- "shared"

    left[[1]] <- shared_child
    left[[2]] <- shared_str
    left[[3]] <- create_fmalloc_vector("list", 1, runtime = rt)

    right[[1]] <- shared_child
    right[[2]] <- shared_str
    right[[3]] <- left[[3]]

    for (step in 1:600L) {
        if ((step %% 2L) == 0L) {
            shared_child[1] <- as.integer(step)
            expect_equal(left[[1]][1], as.integer(step))
            expect_equal(right[[1]][1], as.integer(step))
        } else {
            shared_str[1] <- paste0("x", step)
            expect_equal(left[[2]][1], paste0("x", step))
            expect_equal(right[[2]][1], paste0("x", step))
        }

        if ((step %% 75L) == 0L) {
            gc()
            expect_equal(left[[1]][1], as.integer(if ((step %% 2L) == 0L) step else step - 1L))
            expect_equal(left[[2]][1], paste0("x", if ((step %% 2L) == 1L) step else step - 1L))
            expect_equal(right[[1]][1], as.integer(if ((step %% 2L) == 0L) step else step - 1L))
            expect_equal(right[[2]][1], paste0("x", if ((step %% 2L) == 1L) step else step - 1L))
        }
    }

    # Break and validate shared child references remain valid after dropping one root.
    rm(left)
    gc()
    expect_equal(right[[1]][1], 600L)
    expect_equal(right[[2]][1], "x599")
    message("  Shared children across recursive-like list graphs in scratch mode passed")
})()

message("Scratch-mode GC stress tests for recursive types and strings completed!")
