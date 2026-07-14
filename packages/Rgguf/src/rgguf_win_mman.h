/* Minimal POSIX mmap/munmap shim for Windows (Rtools/mingw-w64), enough for
 * Rgguf's read-only whole-file mapping. It also retains the write flag mapping
 * for completeness.
 *
 * The mapping object is closed right after MapViewOfFile: the view holds a
 * reference to it, so it stays valid until UnmapViewOfFile, and munmap then
 * needs nothing but the address (no handle bookkeeping). This is the standard
 * mman-win32 technique.
 *
 * mingw-w64 already provides the remaining file APIs. */
#ifndef RGGUF_WIN_MMAN_H
#define RGGUF_WIN_MMAN_H

#include <windows.h>
#include <io.h>
#include <sys/types.h>
#include <stddef.h>

#define PROT_NONE   0
#define PROT_READ   1
#define PROT_WRITE  2
#define MAP_SHARED  1
#define MAP_PRIVATE 2
#define MAP_FAILED  ((void *) -1)

static void *mmap(void *addr, size_t length, int prot, int flags,
                  int fd, off_t offset)
{
    (void) addr;
    (void) flags;
    HANDLE fh = (HANDLE) _get_osfhandle(fd);
    if (fh == INVALID_HANDLE_VALUE) {
        return MAP_FAILED;
    }
    DWORD protect = (prot & PROT_WRITE) ? PAGE_READWRITE : PAGE_READONLY;
    DWORD access  = (prot & PROT_WRITE) ? FILE_MAP_WRITE : FILE_MAP_READ;
    HANDLE mh = CreateFileMapping(fh, NULL, protect, 0, 0, NULL);
    if (mh == NULL) {
        return MAP_FAILED;
    }
    ULARGE_INTEGER off;
    off.QuadPart = (unsigned long long) offset;
    void *p = MapViewOfFile(mh, access, off.HighPart, off.LowPart, length);
    CloseHandle(mh); /* the view keeps the mapping alive */
    return p ? p : MAP_FAILED;
}

static int munmap(void *addr, size_t length)
{
    (void) length;
    return UnmapViewOfFile(addr) ? 0 : -1;
}

#endif /* RGGUF_WIN_MMAN_H */
