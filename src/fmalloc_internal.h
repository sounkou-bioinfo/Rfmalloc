#ifndef RFMALLOC_INTERNAL_H
#define RFMALLOC_INTERNAL_H

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <mutex>
#include <new>
#include <random>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <filesystem>
#include <vector>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include "fmalloc.hpp"
#include <R.h>
#include <Rinternals.h>
#include <R_ext/Altrep.h>
#include <R_ext/Rdynload.h>

enum fm_runtime_mode {
    FM_MODE_PERSISTENT = 1,
    FM_MODE_SCRATCH = 2
};

struct fm_runtime {
    struct fm_info *info;
    std::mutex mutex;
    size_t live_vectors;
    size_t external_refs;
    bool close_requested;
    bool close_pending;
    fm_runtime_mode mode;
    std::string filepath;
    uint64_t file_uuid_hi;
    uint64_t file_uuid_lo;

    fm_runtime(struct fm_info *_info, fm_runtime_mode _mode, const char *_filepath,
               uint64_t _uuid_hi, uint64_t _uuid_lo)
        : info(_info), live_vectors(0), external_refs(0), close_requested(false), close_pending(false),
          mode(_mode), filepath(_filepath), file_uuid_hi(_uuid_hi), file_uuid_lo(_uuid_lo) {}
};

struct fm_vector {
    fm_runtime *runtime;
    SEXPTYPE type;
    R_xlen_t len;
    void *data;
    size_t bytes;
    uint64_t catalog_offset;
    uint64_t generation;
    SEXP refs;
    size_t parent_refs;
    bool dataptr_exposed;
    bool maybe_dirty;

    fm_vector(fm_runtime *_runtime, SEXPTYPE _type, R_xlen_t _length, void *_data, size_t _bytes)
        : runtime(_runtime), type(_type), len(_length), data(_data), bytes(_bytes),
          catalog_offset(0), generation(0), refs(R_NilValue), parent_refs(0),
          dataptr_exposed(false), maybe_dirty(false) {}
};

static constexpr uint32_t FM_STRING_FLAG_NA = 1u;

struct fm_string_entry {
    uint64_t offset;
    uint64_t nbytes;
    int32_t encoding;
    uint32_t flags;
};

static constexpr uint64_t RFM_APP_MAGIC = 0x52464d414c545231ULL; // "RFMALTR1"
static constexpr uint32_t RFM_APP_VERSION = 2;
static constexpr uint64_t RFM_CATALOG_MAGIC = 0x52464d4341543031ULL; // "RFMCAT01"
static constexpr uint32_t RFM_CATALOG_VERSION = 1;
static constexpr uint32_t RFM_CATALOG_STATE_IN_PROGRESS = 1;
static constexpr uint32_t RFM_CATALOG_STATE_COMMITTED = 2;
static constexpr uint32_t RFM_CATALOG_STATE_TOMBSTONE = 3;
static constexpr uint32_t RFM_CATALOG_FLAG_RECOVERABLE = 1u;
static constexpr uint32_t RFM_CATALOG_FLAG_STRING_PAYLOADS = 2u;
static constexpr uint32_t RFM_CATALOG_FLAG_POINTER_CONTAINER = 4u;

struct rfm_app_root {
    uint64_t magic;
    uint32_t version;
    uint32_t reserved;
    uint64_t file_uuid_hi;
    uint64_t file_uuid_lo;
    uint64_t flags;
    uint64_t catalog_head_offset;
    uint64_t catalog_epoch;
    uint64_t catalog_count;
    uint64_t reserved_words[499];
};

struct rfm_catalog_record {
    uint64_t magic;
    uint32_t version;
    uint32_t state;
    uint64_t next_offset;
    uint64_t generation;
    int32_t sexptype;
    uint32_t flags;
    uint64_t length;
    uint64_t payload_offset;
    uint64_t payload_nbytes;
    uint64_t reserved_words[8];
};

#endif // RFMALLOC_INTERNAL_H
