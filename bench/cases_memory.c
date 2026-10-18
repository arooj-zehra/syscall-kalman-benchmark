/*
 * Memory cases: anonymous memory (mmap / VirtualAlloc) and file mappings
 * (mmap(fd) / CreateFileMapping+MapViewOfFile).
 *
 * Mapping only reserves address space; physical pages arrive on first touch.
 * So each size has a "map" case (the call alone) and a "map_touch" case (the
 * call plus one write, or read for files, to every 4 KB page). The difference
 * is the page-fault cost. The unmap cases come in untouched and touched flavours
 * because freeing touched pages is the expensive part.
 *
 * Batch sizes are chosen so one batch never holds more than ~128 MB resident or
 * 1 GB of address space (Windows charges committed memory against the page file).
 */

#include "bench.h"
#include "log.h"
#include "platform.h"

#include <stdlib.h>

typedef struct {
    size_t       size;
    int          file;     /* 1 = file mapping */
    void       **addrs;
    char         path[512];
    plat_handle  h;
    volatile unsigned sink;
} mctx;

static int mem_setup_common(void **ctx, const bench_case *c, int file)
{
    mctx *m = (mctx *)calloc(1, sizeof *m);

    if (m == NULL) {
        return -1;
    }
    m->size = c->size;
    m->file = file;
    m->h = PLAT_INVALID_HANDLE;
    m->addrs = (void **)calloc((size_t)c->batch, sizeof *m->addrs);
    if (m->addrs == NULL) {
        free(m);
        return -1;
    }
    if (file) {
        bench_tmp_name(m->path, sizeof m->path, "map.dat");
        if (bench_make_file(m->path, c->size) != 0) {
            free(m->addrs);
            free(m);
            return -1;
        }
        m->h = plat_open(m->path, PLAT_OPEN_READ | PLAT_OPEN_WRITE, 0);
        if (m->h == PLAT_INVALID_HANDLE) {
            (void)plat_unlink(m->path);
            free(m->addrs);
            free(m);
            return -1;
        }
    }
    *ctx = m;
    return 0;
}

static int anon_setup(void **ctx, const bench_case *c) { return mem_setup_common(ctx, c, 0); }
static int file_setup(void **ctx, const bench_case *c) { return mem_setup_common(ctx, c, 1); }

static void mem_teardown(void *ctx)
{
    mctx *m = ctx;

    if (m->file) {
        (void)plat_close(m->h);
        (void)plat_unlink(m->path);
    }
    free(m->addrs);
    free(m);
}

static void *do_map(mctx *m)
{
    return m->file ? plat_map_file(m->h, m->size, 1) : plat_map_anon(m->size);
}

static int do_unmap(mctx *m, int i)
{
    int rc = m->file ? plat_unmap_file(m->addrs[i], m->size) : plat_unmap_anon(m->addrs[i], m->size);

    m->addrs[i] = NULL;
    return rc;
}

static int map_run(void *ctx, int i)
{
    mctx *m = ctx;

    m->addrs[i] = do_map(m);
    return m->addrs[i] == NULL ? -1 : 0;
}

static int map_touch_run(void *ctx, int i)
{
    mctx *m = ctx;

    m->addrs[i] = do_map(m);
    if (m->addrs[i] == NULL) {
        return -1;
    }
    /* Anonymous memory is written (first-touch allocation); file pages are read
     * (they are already in the file cache, so this is a minor fault). */
    m->sink += m->file ? bench_touch_read(m->addrs[i], m->size)
                       : bench_touch_write(m->addrs[i], m->size);
    return 0;
}

static int map_cleanup(void *ctx, int n)
{
    mctx *m = ctx;
    int i;
    int rc = 0;

    for (i = 0; i < n; i++) {
        if (m->addrs[i] != NULL && do_unmap(m, i) != 0) {
            rc = -1;
        }
    }
    return rc;
}

static int unmap_prepare_untouched(void *ctx, int n)
{
    mctx *m = ctx;
    int i;

    for (i = 0; i < n; i++) {
        m->addrs[i] = do_map(m);
        if (m->addrs[i] == NULL) {
            return -1;
        }
    }
    return 0;
}

static int unmap_prepare_touched(void *ctx, int n)
{
    mctx *m = ctx;
    int i;

    if (unmap_prepare_untouched(ctx, n) != 0) {
        return -1;
    }
    for (i = 0; i < n; i++) {
        m->sink += m->file ? bench_touch_read(m->addrs[i], m->size)
                           : bench_touch_write(m->addrs[i], m->size);
    }
    return 0;
}

static int unmap_run(void *ctx, int i)
{
    return do_unmap(ctx, i);
}

/* How many mappings per batch for a given size. */
static int batch_for(size_t size, int touched)
{
    size_t cap = touched ? (128u << 20) : (1024u << 20);
    size_t n = cap / size;

    if (n > 256) {
        n = 256;
    }
    if (n < 4) {
        n = 4;
    }
    return (int)n;
}

void bench_cases_memory(bench_list *list)
{
    static const size_t sizes[] = {4096u, 1048576u, 16777216u};
    bench_case c = {0};
    size_t k;

    c.group = "memory";
    c.warmup = 3;
    c.samples = 40;

    for (k = 0; k < sizeof sizes / sizeof sizes[0]; k++) {
        int file;

        c.size = sizes[k];
        for (file = 0; file <= 1; file++) {
            c.setup = file ? file_setup : anon_setup;
            c.teardown = mem_teardown;

            c.call = file ? "plat_map_file" : "plat_map_anon";
            c.note = "map only, no page touched";
            c.batch = batch_for(c.size, 0);
            c.prepare = NULL;
            c.run = map_run;
            c.cleanup = map_cleanup;
            bench_add(list, &c);

            c.call = file ? "plat_map_file_touch" : "plat_map_anon_touch";
            c.note = file ? "map and read one byte per 4 KB page (cached file)"
                          : "map and write one byte per 4 KB page";
            c.batch = batch_for(c.size, 1);
            c.run = map_touch_run;
            bench_add(list, &c);

            c.call = file ? "plat_unmap_file" : "plat_unmap_anon";
            c.note = "unmap a mapping nothing was touched in";
            c.batch = batch_for(c.size, 0);
            c.prepare = unmap_prepare_untouched;
            c.run = unmap_run;
            c.cleanup = map_cleanup;
            bench_add(list, &c);

            c.call = file ? "plat_unmap_file_touched" : "plat_unmap_anon_touched";
            c.note = "unmap a mapping whose pages were all touched";
            c.batch = batch_for(c.size, 1);
            c.prepare = unmap_prepare_touched;
            bench_add(list, &c);
        }
    }
}
