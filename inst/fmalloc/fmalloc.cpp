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

#include "fmalloc.hpp"

#include <assert.h>
#include <mutex>

#ifdef _WIN32
#include <windows.h>
#include <io.h>
#include <sys/stat.h>
#else
#include <unistd.h>
#include <fcntl.h>
#endif

__thread uint64_t __fm_addr_base = 0;

static std::mutex fm_bitmap_mutex;

extern void do_ptmalloc_init(unsigned long chunk_size);
extern void fmalloc_ptmalloc_set_current_arena(unsigned long chunk_size);

/* init routine */
struct fm_info *fmalloc_init(const char *filepath, bool *init)
{
	struct fm_super *s;
	void *mem;
	uint64_t *magicp;
	size_t len;
	int fd = -1; // Initialize fd for both Windows and POSIX

#ifdef _WIN32
	HANDLE file_handle, map_handle;
	WIN32_FILE_ATTRIBUTE_DATA file_info;
	
	// Get file size on Windows
	if (!GetFileAttributesExA(filepath, GetFileExInfoStandard, &file_info)) {
		fprintf(stderr, "GetFileAttributesEx failed for file: %s\n", filepath);
		exit(1);
	}
	len = ((size_t)file_info.nFileSizeHigh << 32) | file_info.nFileSizeLow;
	
	// Open file on Windows
	file_handle = CreateFileA(filepath, GENERIC_READ | GENERIC_WRITE, 
	                         FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, 
	                         OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (file_handle == INVALID_HANDLE_VALUE) {
		fprintf(stderr, "CreateFile failed for file: %s\n", filepath);
		exit(1);
	}
	
	// Create file mapping on Windows
	map_handle = CreateFileMappingA(file_handle, NULL, PAGE_READWRITE, 0, 0, NULL);
	if (map_handle == NULL) {
		fprintf(stderr, "CreateFileMapping failed for file: %s\n", filepath);
		CloseHandle(file_handle);
		exit(1);
	}
	
	// Map view of file on Windows
	mem = MapViewOfFile(map_handle, FILE_MAP_ALL_ACCESS, 0, 0, 0);
	if (mem == NULL) {
		fprintf(stderr, "MapViewOfFile failed for file: %s\n", filepath);
		CloseHandle(map_handle);
		CloseHandle(file_handle);
		exit(1);
	}
	
	// Store handles for cleanup (you may want to store these globally)
	// For now we'll close them after mapping since Windows keeps the mapping
	CloseHandle(map_handle);
	CloseHandle(file_handle);
	
#else
	struct stat st;
	
	if (stat(filepath, &st) < 0) {
		perror("stat");
		exit(1);
	}

	len = st.st_size;

	fd = open(filepath, O_RDWR, 0644);
	if (fd < 0) {
		perror("open");
		fprintf(stderr, "file: %s (%d)\n", filepath, fd);
		exit(1);
	}

	mem = mmap(0, len, PROT_WRITE | PROT_READ, MAP_SHARED, fd, 0);
	assert(mem != MAP_FAILED);
	
	close(fd);
#endif

	s = (struct fm_super *) mem;

	magicp = (uint64_t *) mem;
	if (*magicp != FMALLOC_MAGIC) {
		int *initialized;

		s->magic = FMALLOC_MAGIC;
		s->set_total_size(len);
		s->bitmap_set(0);

		initialized = (int *)((uint64_t) mem + FMALLOC_OFF);
		*initialized = -1;
		*init = true;
	}

	__fm_addr_base = (uint64_t) mem;

	do_ptmalloc_init(s->chunk_size);
	fmalloc_ptmalloc_set_current_arena(s->chunk_size);

	return new fm_info(fd, mem, s);
}

void fmalloc_set_target(struct fm_info *fi)
{
	__fm_addr_base = (uint64_t) fi->mem;
	fmalloc_ptmalloc_set_current_arena(fi->s->chunk_size);
}

void *fmalloc(size_t size)
{
	return dlmalloc(size);
}

void ffree(void *addr)
{
	dlfree(addr);
}

size_t fmalloc_mmap_round(size_t length)
{
	if (__fm_addr_base == 0 || length == 0)
		return length;

	struct fm_super *s = (struct fm_super *) __fm_addr_base;
	if (s->magic != FMALLOC_MAGIC || s->chunk_size == 0)
		return length;

	size_t chunk_size = (size_t) s->chunk_size;
	if (length > (~(size_t) 0) - (chunk_size - 1))
		return length;
	return ((length + chunk_size - 1) / chunk_size) * chunk_size;
}

void *fmalloc_mmap(size_t length)
{
	if (__fm_addr_base == 0 || length == 0)
		return MAP_FAILED;

	struct fm_super *s = (struct fm_super *) __fm_addr_base;
	if (s->magic != FMALLOC_MAGIC)
		return MAP_FAILED;

	std::lock_guard<std::mutex> lock(fm_bitmap_mutex);
	return s->mmap_locked(length);
}

int fmalloc_munmap(void *addr, size_t length)
{
	if (__fm_addr_base == 0 || addr == NULL || length == 0)
		return 0;

	struct fm_super *s = (struct fm_super *) __fm_addr_base;
	if (s->magic != FMALLOC_MAGIC)
		return -1;

	std::lock_guard<std::mutex> lock(fm_bitmap_mutex);
	return s->munmap_locked(addr, length);
}
