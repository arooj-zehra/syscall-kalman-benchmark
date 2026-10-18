/* Helpers shared by the benchmark case files. */

#include "bench.h"
#include "log.h"
#include "platform.h"

#include <stdio.h>
#include <stdlib.h>

bench_env g_env = {"", "", 0};

#define PAGE 4096u

void bench_tmp_name(char *buf, size_t n, const char *tag)
{
    snprintf(buf, n, "%ssk_bench_%lu_%s", g_env.tmpdir, plat_getpid(), tag);
}

int bench_make_file(const char *path, size_t size)
{
    enum { CHUNK = 65536 };
    unsigned char *buf;
    plat_handle h;
    size_t done = 0;
    size_t i;

    buf = (unsigned char *)malloc(CHUNK);
    if (buf == NULL) {
        return -1;
    }
    for (i = 0; i < CHUNK; i++) {
        buf[i] = (unsigned char)((i * 7u + 3u) & 0xffu);
    }
    h = plat_open(path, PLAT_OPEN_WRITE | PLAT_OPEN_CREATE | PLAT_OPEN_TRUNC, 0644);
    if (h == PLAT_INVALID_HANDLE) {
        free(buf);
        return -1;
    }
    while (done < size) {
        size_t want = size - done < CHUNK ? size - done : CHUNK;
        int64_t n = plat_write(h, buf, want);

        if (n <= 0) {
            (void)plat_close(h);
            free(buf);
            return -1;
        }
        done += (size_t)n;
    }
    free(buf);
    return plat_close(h);
}

unsigned bench_touch_write(void *p, size_t len)
{
    volatile unsigned char *b = (volatile unsigned char *)p;
    unsigned sum = 0;
    size_t i;

    for (i = 0; i < len; i += PAGE) {
        b[i] = (unsigned char)i;
        sum += 1u;
    }
    return sum;
}

unsigned bench_touch_read(const void *p, size_t len)
{
    const volatile unsigned char *b = (const volatile unsigned char *)p;
    unsigned sum = 0;
    size_t i;

    for (i = 0; i < len; i += PAGE) {
        sum += b[i];
    }
    return sum;
}
