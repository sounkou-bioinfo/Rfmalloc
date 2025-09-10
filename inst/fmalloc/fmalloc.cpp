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

#ifdef _WIN32
#include <windows.h>
#include <io.h>
#else
#include <unistd.h>
#include <fcntl.h>
#endif

__thread uint64_t __fm_addr_base = 0;

extern void do_ptmalloc_init(unsigned long chunk_size);

/* init routine */
struct fm_info *fmalloc_init(const char *filepath, bool *init)
{
	struct fm_super *s;
	void *mem;
	uint64_t *magicp;
	size_t len;

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
	int fd;
	
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

	return new fm_info(fd, mem, s);
}

void fmalloc_set_target(struct fm_info *fi)
{
	__fm_addr_base = (uint64_t) fi->mem;
}

void *fmalloc(size_t size)
{
	return dlmalloc(size);
}

void ffree(void *addr)
{
	dlfree(addr);
}
