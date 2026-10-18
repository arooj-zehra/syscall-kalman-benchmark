/*
 * demo_memory.c - memory mapping.
 *
 * Linux: mmap / munmap.
 * Windows: VirtualAlloc / VirtualFree for anonymous memory, CreateFileMapping +
 * MapViewOfFile / UnmapViewOfFile for file-backed memory.
 *
 *   1. Anonymous memory: zero-filled, writable, released again.
 *   2. Read-only file mapping: bytes seen through the mapping equal what was written.
 *   3. Writable file mapping: a change made through memory reaches the file.
 *   4. A forced error: mapping an invalid handle.
 */

#include "demo_util.h"
#include "platform.h"

#include <stdint.h>

#define FILE_NAME "sk_memory.tmp"
#define ANON_LEN (1024u * 1024u)
#define FILE_LEN 65536u

static int make_pattern_file(void)
{
    unsigned char block[4096];
    plat_handle h = plat_open(FILE_NAME, PLAT_OPEN_WRITE | PLAT_OPEN_CREATE | PLAT_OPEN_TRUNC, 0644);
    size_t off;
    size_t i;
    int ok = 1;

    if (h == PLAT_INVALID_HANDLE) {
        return -1;
    }
    for (off = 0; off < FILE_LEN; off += sizeof block) {
        for (i = 0; i < sizeof block; i++) {
            block[i] = DEMO_PATTERN(off + i);
        }
        if (plat_write(h, block, sizeof block) != (int64_t)sizeof block) {
            ok = 0;
            break;
        }
    }
    if (plat_close(h) != 0) {
        ok = 0;
    }
    return ok ? 0 : -1;
}

int main(int argc, char **argv)
{
    int failures = 0;
    unsigned char *p;
    plat_handle h;
    size_t i;
    int match;

    demo_begin(argc, argv, "memory");

    /* ---- 1. anonymous memory ---- */
    p = (unsigned char *)plat_map_anon(ANON_LEN);
    DEMO_CHECK(failures, p != NULL, "mapped 1 MiB of anonymous memory");
    if (p != NULL) {
        DEMO_CHECK(failures, p[0] == 0 && p[ANON_LEN / 2] == 0 && p[ANON_LEN - 1] == 0,
                   "new anonymous memory is zero-filled");
        for (i = 0; i < ANON_LEN; i++) {
            p[i] = DEMO_PATTERN(i);
        }
        match = 1;
        for (i = 0; i < ANON_LEN; i++) {
            if (p[i] != DEMO_PATTERN(i)) {
                match = 0;
                break;
            }
        }
        DEMO_CHECK(failures, match, "wrote and read back 1 MiB through the mapping");
        DEMO_CHECK(failures, plat_unmap_anon(p, ANON_LEN) == 0, "released the anonymous memory");
    }

    /* ---- 2. read-only file mapping ---- */
    DEMO_CHECK(failures, make_pattern_file() == 0, "created a 64 KB pattern file");
    h = plat_open(FILE_NAME, PLAT_OPEN_READ, 0);
    DEMO_CHECK(failures, h != PLAT_INVALID_HANDLE, "opened the file for reading");
    if (h != PLAT_INVALID_HANDLE) {
        p = (unsigned char *)plat_map_file(h, FILE_LEN, 0);
        DEMO_CHECK(failures, p != NULL, "mapped the file read-only");
        if (p != NULL) {
            match = 1;
            for (i = 0; i < FILE_LEN; i++) {
                if (p[i] != DEMO_PATTERN(i)) {
                    match = 0;
                    break;
                }
            }
            DEMO_CHECK(failures, match, "bytes seen through the mapping equal the bytes written");
            DEMO_CHECK(failures, plat_unmap_file(p, FILE_LEN) == 0, "unmapped the file");
        }
        (void)plat_close(h);
    }

    /* ---- 3. writable file mapping ---- */
    h = plat_open(FILE_NAME, PLAT_OPEN_READ | PLAT_OPEN_WRITE, 0);
    DEMO_CHECK(failures, h != PLAT_INVALID_HANDLE, "opened the file for reading and writing");
    if (h != PLAT_INVALID_HANDLE) {
        p = (unsigned char *)plat_map_file(h, FILE_LEN, 1);
        DEMO_CHECK(failures, p != NULL, "mapped the file read-write");
        if (p != NULL) {
            p[0] = 0xEE;
            p[FILE_LEN - 1] = 0xDD;
            DEMO_CHECK(failures, plat_unmap_file(p, FILE_LEN) == 0, "unmapped after writing through memory");
        }
        (void)plat_close(h);
    }
    h = plat_open(FILE_NAME, PLAT_OPEN_READ, 0);
    if (h != PLAT_INVALID_HANDLE) {
        unsigned char data[FILE_LEN];
        int64_t n = plat_read(h, data, sizeof data);

        DEMO_CHECK(failures, n == (int64_t)FILE_LEN && data[0] == 0xEE && data[FILE_LEN - 1] == 0xDD,
                   "a change made through the mapping reached the file");
        (void)plat_close(h);
    }

    /* ---- 4. forced error ---- */
    LOG_INFO("mapping an invalid handle; the ERROR line that follows is expected");
    DEMO_CHECK(failures, plat_map_file(PLAT_INVALID_HANDLE, 4096, 0) == NULL, "mapping an invalid handle fails");

    DEMO_CHECK(failures, plat_unlink(FILE_NAME) == 0, "removed the temporary file");
    return demo_finish("memory", failures);
}
