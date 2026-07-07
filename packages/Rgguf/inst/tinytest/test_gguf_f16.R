library(tinytest)
library(Rgguf)

message("Testing F16 dequantization against a hand-built GGUF file...")

# gguf_write_tensors() only ever emits f32 tensors (see ?gguf_write_tensors),
# so to exercise the F16 read path (fp16.c's from_half(), wired up in
# src/rgguf_read.c) this test hand-builds a minimal GGUF file byte-for-byte,
# following the format documented in the vendored src/gguflib.h/.c:
#   header:        magic "GGUF", uint32 version, uint64 tensor_count,
#                   uint64 metadata_kv_count
#   tensor info:    uint64 namelen, name bytes, uint32 ndim,
#                   ndim x uint64 dim[], uint32 type, uint64 offset
#   [alignment padding to a multiple of 32 bytes]
#   tensor data:    raw bytes
#
# All integers are little-endian, which is what GGUF specifies and what
# every realistic test host already is; values here are also all small
# enough that only writeBin()'s low-order bytes differ from a true 64-bit
# write, so we can emit "uint64" fields as a 32-bit value followed by 4
# zero bytes.

write_u32 <- function(con, v) writeBin(as.integer(v), con, size = 4, endian = "little")
write_u64 <- function(con, v) {
    write_u32(con, v)
    write_u32(con, 0L)
}
write_u16 <- function(con, v) {
    # v are unsigned 16-bit bit patterns (0..65535); writeBin's 2-byte
    # integer path is signed, so fold values >= 32768 into the equivalent
    # negative 16-bit two's complement representation.
    v <- ifelse(v >= 32768L, v - 65536L, v)
    writeBin(as.integer(v), con, size = 2, endian = "little")
}

build_f16_gguf <- function(path, name, f16_bits) {
    con <- file(path, open = "wb")
    on.exit(close(con))

    writeBin(charToRaw("GGUF"), con)
    write_u32(con, 3L) # version
    write_u64(con, 1L) # tensor_count
    write_u64(con, 0L) # metadata_kv_count

    write_u64(con, nchar(name, type = "bytes")) # namelen
    writeBin(charToRaw(name), con)
    write_u32(con, 1L) # ndim
    write_u64(con, length(f16_bits)) # dim[0]
    write_u32(con, 1L) # type = GGUF_TYPE_F16
    write_u64(con, 0L) # offset (relative to data section)

    header_and_info_size <- 24 + (8 + nchar(name, type = "bytes") + 4 + 8 + 4 + 8)
    alignment <- 32
    padding <- (alignment - (header_and_info_size %% alignment)) %% alignment
    if (padding > 0) writeBin(raw(padding), con)

    write_u16(con, f16_bits)
}

(function() {
    message("  Test 1: known f16 bit patterns dequantize to their exact decimal values")
    tmp <- tempfile(fileext = ".gguf")
    rt <- Rfmalloc::open_fmalloc(tempfile(fileext = ".bin"), mode = "scratch")
    on.exit({
        unlink(tmp)
        Rfmalloc::cleanup_fmalloc(rt)
    }, add = TRUE)

    # 0.0, 1.0, 2.0, -2.0, 0.5 as IEEE-754 half-precision bit patterns.
    bits <- c(0x0000, 0x3C00, 0x4000, 0xC000, 0x3800)
    expected <- c(0, 1, 2, -2, 0.5)

    build_f16_gguf(tmp, "t", bits)

    tbl <- gguf_tensors(tmp)
    expect_equal(tbl$type, "f16")

    got <- gguf_tensor(tmp, "t", runtime = rt)
    expect_equal(as.vector(got), expected)
    expect_true(Rfmalloc::is_fmalloc_vector(got))

    message("  F16 dequantization test passed")
})()
