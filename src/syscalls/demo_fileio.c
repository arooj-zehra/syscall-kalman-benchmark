/*
 * demo_fileio.c - open / read / write / close (Linux)  =  CreateFile / ReadFile /
 * WriteFile / CloseHandle (Windows).
 *
 * Writes a 64 KB file in 4 KB blocks, reads it back, compares checksums and size,
 * then forces three errors to show that failures are detected and logged.
 */

#include "demo_util.h"
#include "platform.h"

#include <stdint.h>

#define FILE_NAME "sk_fileio.tmp"
#define BLOCK 4096
#define BLOCKS 16

#define FNV_OFFSET 1469598103934665603ull
#define FNV_PRIME 1099511628211ull

static uint64_t fnv(uint64_t h, const unsigned char *p, size_t n)
{
    size_t i;
    for (i = 0; i < n; i++) {
        h ^= p[i];
        h *= FNV_PRIME;
    }
    return h;
}

int main(int argc, char **argv)
{
    int failures = 0;
    unsigned char block[BLOCK];
    uint64_t sum_written = FNV_OFFSET;
    uint64_t sum_read = FNV_OFFSET;
    int64_t total_written = 0;
    int64_t total_read = 0;
    plat_handle h;
    plat_stat st;
    int b;
    size_t i;

    demo_begin(argc, argv, "fileio");

    /* ---- write ---- */
    h = plat_open(FILE_NAME, PLAT_OPEN_WRITE | PLAT_OPEN_CREATE | PLAT_OPEN_TRUNC, 0644);
    DEMO_CHECK(failures, h != PLAT_INVALID_HANDLE, "open for writing");
    if (h == PLAT_INVALID_HANDLE) {
        return demo_finish("fileio", failures);
    }
    for (b = 0; b < BLOCKS; b++) {
        int64_t n;
        for (i = 0; i < BLOCK; i++) {
            block[i] = DEMO_PATTERN((size_t)b * BLOCK + i);
        }
        n = plat_write(h, block, BLOCK);
        if (n != BLOCK) {
            LOG_ERROR("short or failed write at block %d (%lld)", b, (long long)n);
            failures++;
            break;
        }
        total_written += n;
        sum_written = fnv(sum_written, block, BLOCK);
    }
    DEMO_CHECK(failures, total_written == (int64_t)BLOCK * BLOCKS, "wrote 16 blocks of 4096 bytes");
    DEMO_CHECK(failures, plat_close(h) == 0, "close after writing");

    DEMO_CHECK(failures, plat_stat_path(FILE_NAME, &st) == 0 && st.size == (int64_t)BLOCK * BLOCKS,
               "file size on disk is 65536 bytes");

    /* ---- read back ---- */
    h = plat_open(FILE_NAME, PLAT_OPEN_READ, 0);
    DEMO_CHECK(failures, h != PLAT_INVALID_HANDLE, "open for reading");
    if (h != PLAT_INVALID_HANDLE) {
        for (;;) {
            int64_t n = plat_read(h, block, BLOCK);
            if (n < 0) {
                failures++;
                break;
            }
            if (n == 0) {
                break; /* end of file */
            }
            total_read += n;
            sum_read = fnv(sum_read, block, (size_t)n);
        }
        DEMO_CHECK(failures, plat_close(h) == 0, "close after reading");
    }
    DEMO_CHECK(failures, total_read == total_written, "read back the same number of bytes");
    DEMO_CHECK(failures, sum_read == sum_written, "checksum of data read equals checksum of data written");

    /* ---- forced errors (each logs an ERROR line; that is expected here) ---- */
    LOG_INFO("forcing three errors; the ERROR lines that follow are expected");
    h = plat_open("sk_no_such_dir/missing.bin", PLAT_OPEN_READ, 0);
    DEMO_CHECK(failures, h == PLAT_INVALID_HANDLE, "open of a missing path fails");
    DEMO_CHECK(failures, plat_read(PLAT_INVALID_HANDLE, block, BLOCK) == -1, "read on an invalid handle fails");

    h = plat_open(FILE_NAME, PLAT_OPEN_READ, 0);
    if (h != PLAT_INVALID_HANDLE) {
        (void)plat_close(h);
        DEMO_CHECK(failures, plat_close(h) == -1, "closing the same handle twice fails");
    }

    /* ---- cleanup ---- */
    DEMO_CHECK(failures, plat_unlink(FILE_NAME) == 0, "removed the temporary file");

    return demo_finish("fileio", failures);
}
