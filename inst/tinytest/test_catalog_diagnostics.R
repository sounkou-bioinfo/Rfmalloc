library(tinytest)
library(Rfmalloc)

message("Testing catalog diagnostics tooling...")

(function() {
    message("  Test 1: Runtime diagnostics report catalog activity and tombstones")
    tmp <- tempfile(fileext = ".bin")
    on.exit({
        cleanup_fmalloc(rt)
        unlink(tmp)
    }, add = TRUE)

    rt <- open_fmalloc(tmp, mode = "persistent")
    a <- create_fmalloc_vector("integer", 4, runtime = rt)
    b <- create_fmalloc_vector("character", 3, runtime = rt)
    a[] <- c(1L, 2L, 3L, 4L)
    b[] <- c("alpha", "beta", "gamma")

    before <- diagnose_fmalloc_runtime(rt)
    expect_equal(before$runtime$mode, "persistent")
    expect_equal(before$runtime$runtime_open, TRUE)
    expect_true(before$summary$record_count >= 2L)
    expect_true(before$summary$committed_records >= 2L)
    expect_true(before$summary$potentially_reclaimable_payload_bytes >= 0)

    expect_true(destroy_fmalloc_vector(a))
    after <- diagnose_fmalloc_runtime(rt)
    expect_equal(after$runtime$mode, "persistent")
    expect_equal(after$summary$committed_records, before$summary$committed_records - 1L)
    expect_equal(after$summary$tombstoned_records, before$summary$tombstoned_records + 1L)
    expect_false(after$summary$compaction_implemented)
    expect_true(nchar(after$summary$compaction_note) > 10L)
    expect_true(after$summary$potentially_reclaimable_payload_bytes >=
                    after$summary$tombstoned_payload_bytes)

    message("  Runtime diagnostics test passed")
})()

(function() {
    message("  Test 2: Scratch runtime diagnostics remain empty by design")
    tmp <- tempfile(fileext = ".bin")
    on.exit({
        cleanup_fmalloc(rt)
        unlink(tmp)
    }, add = TRUE)

    rt <- open_fmalloc(tmp, mode = "scratch")
    x <- create_fmalloc_vector("integer", 8, runtime = rt)
    x[] <- 1:8

    before <- diagnose_fmalloc_runtime(rt)
    expect_equal(before$runtime$mode, "scratch")
    expect_equal(before$summary$record_count, 0L)
    expect_equal(before$summary$committed_records, 0L)
    expect_equal(before$summary$tombstoned_records, 0L)

    message("  Scratch diagnostics test passed")
})()

message("Catalog diagnostics tests completed")
