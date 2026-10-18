/*
 * File I/O cases: plat_open, plat_close, plat_read, plat_write (+ plat_unlink).
 *
 * All reads are served from the OS file cache ("warm"): the file was just
 * written. Writes go to the cache and are never flushed by the test, so they
 * measure the system call, not the disk. Temporary files go in --tmpdir.
 */

#include "bench.h"
#include "log.h"
#include "platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char         path[512];
    size_t       size;
    int          batch;
    plat_handle  h;
    plat_handle *hs;
    char       (*names)[544];
    unsigned char *buf;
} fio;

static fio *fio_new(const bench_case *c)
{
    fio *f = (fio *)calloc(1, sizeof *f);

    if (f == NULL) {
        return NULL;
    }
    f->size = c->size;
    f->batch = c->batch;
    f->h = PLAT_INVALID_HANDLE;
    f->hs = (plat_handle *)calloc((size_t)c->batch, sizeof *f->hs);
    f->names = calloc((size_t)c->batch, sizeof *f->names);
    f->buf = (unsigned char *)malloc(c->size > 0 ? c->size : 1);
    if (f->hs == NULL || f->names == NULL || f->buf == NULL) {
        free(f->hs);
        free(f->names);
        free(f->buf);
        free(f);
        return NULL;
    }
    memset(f->buf, 0x5a, c->size > 0 ? c->size : 1);
    bench_tmp_name(f->path, sizeof f->path, "fileio.dat");
    return f;
}

static void fio_free(fio *f)
{
    int i;

    if (f == NULL) {
        return;
    }
    for (i = 0; i < f->batch; i++) {
        if (f->hs[i] != PLAT_INVALID_HANDLE && f->hs[i] != 0) {
            (void)plat_close(f->hs[i]);
        }
    }
    free(f->hs);
    free(f->names);
    free(f->buf);
    free(f);
}

static void name_for(fio *f, int i, char *out, size_t n)
{
    snprintf(out, n, "%s.%d", f->path, i);
}

/* ---- plat_open (existing file) / plat_close ----------------------------------- */

static int open_setup(void **ctx, const bench_case *c)
{
    fio *f = fio_new(c);

    if (f == NULL || bench_make_file(f->path, 4096) != 0) {
        fio_free(f);
        return -1;
    }
    *ctx = f;
    return 0;
}

static void open_teardown(void *ctx)
{
    fio *f = ctx;
    (void)plat_unlink(f->path);
    fio_free(f);
}

static int open_run(void *ctx, int i)
{
    fio *f = ctx;

    f->hs[i] = plat_open(f->path, PLAT_OPEN_READ, 0);
    return f->hs[i] == PLAT_INVALID_HANDLE ? -1 : 0;
}

static int open_cleanup(void *ctx, int n)
{
    fio *f = ctx;
    int i;
    int rc = 0;

    for (i = 0; i < n; i++) {
        if (f->hs[i] != PLAT_INVALID_HANDLE && plat_close(f->hs[i]) != 0) {
            rc = -1;
        }
        f->hs[i] = PLAT_INVALID_HANDLE;
    }
    return rc;
}

static int close_prepare(void *ctx, int n)
{
    fio *f = ctx;
    int i;

    for (i = 0; i < n; i++) {
        f->hs[i] = plat_open(f->path, PLAT_OPEN_READ, 0);
        if (f->hs[i] == PLAT_INVALID_HANDLE) {
            return -1;
        }
    }
    return 0;
}

static int close_run(void *ctx, int i)
{
    fio *f = ctx;
    int rc = plat_close(f->hs[i]);

    f->hs[i] = PLAT_INVALID_HANDLE;
    return rc;
}

/* ---- plat_open with create / plat_unlink --------------------------------------- */

static int create_setup(void **ctx, const bench_case *c)
{
    fio *f = fio_new(c);

    if (f == NULL) {
        return -1;
    }
    *ctx = f;
    return 0;
}

static int create_run(void *ctx, int i)
{
    fio *f = ctx;

    name_for(f, i, f->names[i], sizeof f->names[i]);
    f->hs[i] = plat_open(f->names[i], PLAT_OPEN_WRITE | PLAT_OPEN_CREATE | PLAT_OPEN_TRUNC, 0644);
    return f->hs[i] == PLAT_INVALID_HANDLE ? -1 : 0;
}

static int create_cleanup(void *ctx, int n)
{
    fio *f = ctx;
    int i;
    int rc = 0;

    for (i = 0; i < n; i++) {
        if (f->hs[i] != PLAT_INVALID_HANDLE && plat_close(f->hs[i]) != 0) {
            rc = -1;
        }
        f->hs[i] = PLAT_INVALID_HANDLE;
        if (plat_unlink(f->names[i]) != 0) {
            rc = -1;
        }
    }
    return rc;
}

static void create_teardown(void *ctx)
{
    fio_free(ctx);
}

static int unlink_prepare(void *ctx, int n)
{
    fio *f = ctx;
    int i;

    for (i = 0; i < n; i++) {
        plat_handle h;

        name_for(f, i, f->names[i], sizeof f->names[i]);
        h = plat_open(f->names[i], PLAT_OPEN_WRITE | PLAT_OPEN_CREATE | PLAT_OPEN_TRUNC, 0644);
        if (h == PLAT_INVALID_HANDLE || plat_close(h) != 0) {
            return -1;
        }
    }
    return 0;
}

static int unlink_run(void *ctx, int i)
{
    fio *f = ctx;

    return plat_unlink(f->names[i]);
}

/* ---- plat_read ------------------------------------------------------------------ */

/* The file holds batch * size bytes; each timed call reads the next `size` bytes,
 * so there is no seek and every call reads fresh (but cached) data. */
static int read_setup(void **ctx, const bench_case *c)
{
    fio *f = fio_new(c);

    if (f == NULL || bench_make_file(f->path, c->size * (size_t)c->batch) != 0) {
        fio_free(f);
        return -1;
    }
    *ctx = f;
    return 0;
}

static int read_prepare(void *ctx, int n)
{
    fio *f = ctx;

    (void)n;
    f->h = plat_open(f->path, PLAT_OPEN_READ, 0);
    return f->h == PLAT_INVALID_HANDLE ? -1 : 0;
}

static int read_run(void *ctx, int i)
{
    fio *f = ctx;

    (void)i;
    return plat_read(f->h, f->buf, f->size) == (int64_t)f->size ? 0 : -1;
}

static int read_cleanup(void *ctx, int n)
{
    fio *f = ctx;
    int rc = plat_close(f->h);

    (void)n;
    f->h = PLAT_INVALID_HANDLE;
    return rc;
}

/* ---- plat_write ----------------------------------------------------------------- */

static int write_prepare(void *ctx, int n)
{
    fio *f = ctx;

    (void)n;
    f->h = plat_open(f->path, PLAT_OPEN_WRITE | PLAT_OPEN_CREATE | PLAT_OPEN_TRUNC, 0644);
    return f->h == PLAT_INVALID_HANDLE ? -1 : 0;
}

static int write_run(void *ctx, int i)
{
    fio *f = ctx;

    (void)i;
    return plat_write(f->h, f->buf, f->size) == (int64_t)f->size ? 0 : -1;
}

/* ---- registration ---------------------------------------------------------------- */

void bench_cases_fileio(bench_list *list)
{
    static const struct {
        size_t size;
        int    batch;
        int    samples;
    } sizes[] = {
        {4096u, 1000, 50},
        {65536u, 200, 40},
        {1048576u, 32, 30},
    };
    bench_case c = {0};
    size_t k;

    c.group = "fileio";
    c.warmup = 3;

    c.call = "plat_open";
    c.note = "open an existing file read-only";
    c.batch = 256;
    c.samples = 50;
    c.setup = open_setup;
    c.run = open_run;
    c.cleanup = open_cleanup;
    c.teardown = open_teardown;
    bench_add(list, &c);

    c.call = "plat_open_create";
    c.note = "open with create+truncate: makes a new empty file";
    c.batch = 128;
    c.setup = create_setup;
    c.prepare = NULL;
    c.run = create_run;
    c.cleanup = create_cleanup;
    c.teardown = create_teardown;
    bench_add(list, &c);

    c.call = "plat_close";
    c.note = "close a file handle";
    c.batch = 256;
    c.setup = open_setup;
    c.prepare = close_prepare;
    c.run = close_run;
    c.cleanup = NULL;
    c.teardown = open_teardown;
    bench_add(list, &c);

    c.call = "plat_unlink";
    c.note = "helper: delete an empty file";
    c.batch = 128;
    c.setup = create_setup;
    c.prepare = unlink_prepare;
    c.run = unlink_run;
    c.cleanup = NULL;
    c.teardown = create_teardown;
    bench_add(list, &c);

    for (k = 0; k < sizeof sizes / sizeof sizes[0]; k++) {
        c.size = sizes[k].size;
        c.batch = sizes[k].batch;
        c.samples = sizes[k].samples;

        c.call = "plat_read";
        c.note = "sequential read from the file cache";
        c.setup = read_setup;
        c.prepare = read_prepare;
        c.run = read_run;
        c.cleanup = read_cleanup;
        c.teardown = open_teardown;
        bench_add(list, &c);

        c.call = "plat_write";
        c.note = "sequential write into the file cache, no flush";
        c.setup = create_setup;
        c.prepare = write_prepare;
        c.run = write_run;
        c.cleanup = read_cleanup;
        c.teardown = open_teardown;
        bench_add(list, &c);
    }
}
