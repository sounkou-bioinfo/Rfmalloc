/*
 *
 * Copyright 2020 Kenichi Yasukata
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#ifndef _FMALLOC_H
#define _FMALLOC_H

#include <atomic>
#include <vector>
#include <algorithm>

#include <stdio.h>
#include <stdint.h>
#include <fcntl.h>
#include <string.h>
#ifdef _WIN32
#include <sys/stat.h>
#else
#include <sys/stat.h>
#endif
#ifndef _WIN32
#include <sys/mman.h>
#else
// Define MAP_FAILED for Windows compatibility
#define MAP_FAILED ((void *) -1)
#endif

#ifndef USE_DL_PREFIX
#define USE_DL_PREFIX 1
#endif

#include "malloc-2.8.3.h"

#include "fmptr.hpp"

#ifndef PAGE_SIZE
#define PAGE_SIZE (4096)
#endif

#ifndef FMALLOC_MAGIC
#define FMALLOC_MAGIC (123457)
#endif

#define FMALLOC_OFF (PAGE_SIZE * 2)
#define FMALLOC_MIN_CHUNK (1UL << 24)

/*
 * data layout on a file
 *
 * 0             4KB           8KB                 end
 * |-- fm_super --|-- for app --|-- ... malloc ...--|
 *
 * 4KB ~ 8KB is reserved for app, and assumes to have
 * a pointer to a root object.
 */

/*
 * on-disk representation of super block
 * this always occupies first 4KB of a file
 */
struct fm_super {
	uint64_t magic;
	uint64_t total_size;
	uint64_t chunk_size;
	uint64_t num_chunk;
	uint8_t bm[0]; /* this must be at the end */

	void set_total_size(uint64_t size)
	{
		this->total_size = (size / PAGE_SIZE) * PAGE_SIZE;

		uint64_t usable_size = 0;
		if (this->total_size > FMALLOC_OFF)
			usable_size = this->total_size - FMALLOC_OFF;

		if (usable_size == 0) {
			num_chunk = 0;
			chunk_size = 0;
		} else if (usable_size <= 2 * FMALLOC_MIN_CHUNK) {
			num_chunk = 1;
			chunk_size = (usable_size / PAGE_SIZE) * PAGE_SIZE;
		} else {
			uint64_t max_bitmap_chunks = (PAGE_SIZE - sizeof(struct fm_super)) * 8;
			num_chunk = max_bitmap_chunks;
			chunk_size = ((usable_size / PAGE_SIZE) * PAGE_SIZE) / num_chunk;
			chunk_size = (chunk_size / PAGE_SIZE) * PAGE_SIZE;
			if (chunk_size < FMALLOC_MIN_CHUNK) {
				chunk_size = FMALLOC_MIN_CHUNK;
				num_chunk = usable_size / chunk_size;
			}
			if (num_chunk > max_bitmap_chunks)
				num_chunk = max_bitmap_chunks;
		}
	}

	void munmap_locked(void *mem)
	{
		(void) munmap_locked(mem, chunk_size);
	}

	int munmap_locked(void *mem, size_t length)
	{
		if (!mem || length == 0 || chunk_size == 0)
			return 0;
		int idx = m2i(data_base(), mem);
		if (idx < 0 || (uint64_t) idx >= num_chunk) {
			fprintf(stderr, "fmalloc munmap invalid pointer %p\n", mem);
			return -1;
		}
		uint64_t nchunks = (length + chunk_size - 1) / chunk_size;
		if ((uint64_t) idx + nchunks > num_chunk) {
			fprintf(stderr, "fmalloc munmap range outside backing file\n");
			return -1;
		}
		bitmap_release_run(idx, nchunks);
		return 0;
	}

	void *mmap_locked(void)
	{
		return mmap_locked(chunk_size);
	}

	void *mmap_locked(size_t length)
	{
		if (length == 0 || chunk_size == 0)
			return MAP_FAILED;
		uint64_t nchunks = (length + chunk_size - 1) / chunk_size;
		int idx = bitmap_grab_run(nchunks);
		if (idx < 0) {
			fprintf(stderr, "bitmap_grab failed for %llu chunks (%zu bytes)\n",
				    (unsigned long long) nchunks, length);
			return MAP_FAILED;
		}
		return i2m(data_base(), idx);
	}

	void bitmap_set(int idx)
	{
		bm[idx / 8] |= (1UL << (idx % 8));
	}

	void bitmap_release(uint8_t *bm, int idx)
	{
		bm[idx / 8] &= ~(1UL << (idx % 8));
	}

	int bitmap_grab(uint8_t *bm)
	{
		(void) bm;
		return bitmap_grab_run(1);
	}

	int bitmap_grab_run(uint64_t nchunks)
	{
		if (nchunks == 0 || nchunks > num_chunk)
			return -1;

		uint64_t run_start = 0;
		uint64_t run_length = 0;
		for (uint64_t idx = 0; idx < num_chunk; idx++) {
			if (!bitmap_test(idx)) {
				if (run_length == 0)
					run_start = idx;
				run_length++;
				if (run_length == nchunks) {
					for (uint64_t set_idx = run_start; set_idx < run_start + nchunks; set_idx++)
						bitmap_set((int) set_idx);
					return (int) run_start;
				}
			} else {
				run_length = 0;
			}
		}
		return -1;
	}

	void bitmap_release_run(uint64_t idx, uint64_t nchunks)
	{
		for (uint64_t release_idx = idx; release_idx < idx + nchunks; release_idx++)
			bitmap_release(bm, (int) release_idx);
	}

	bool bitmap_test(uint64_t idx)
	{
		return (bm[idx / 8] & (1UL << (idx % 8))) != 0;
	}

	void *data_base()
	{
		return (void *) ((uint64_t) this + FMALLOC_OFF);
	}

	int m2i(void *mem, void *ptr)
	{
		if (!ptr || chunk_size == 0 || (uint64_t) ptr < (uint64_t) mem)
			return -1;
		uint64_t offset = (uint64_t) ptr - (uint64_t) mem;
		if (offset % chunk_size != 0)
			return -1;
		return (int) (offset / chunk_size);
	}

	void *i2m(void *mem, int idx)
	{
		return (void *) ((uint64_t) mem + (uint64_t) (chunk_size * idx));
	}
} __attribute__((packed));

/* in-memory reference to super block */
struct fm_info {
	int fd;
	void *mem;
	struct fm_super *s;

	fm_info(int _fd, void *_mem, struct fm_super *_s) : fd(_fd), mem(_mem), s(_s)
	{
	}
};

struct fm_info *fmalloc_init(const char *filepath, bool *init);
void fmalloc_set_target(struct fm_info *fi);

void *fmalloc(size_t size);
void ffree(void *addr);

/* File-backed replacements for dlmalloc's internal mmap/munmap hooks. */
size_t fmalloc_mmap_round(size_t length);
void *fmalloc_mmap(size_t length);
int fmalloc_munmap(void *addr, size_t length);

#endif
