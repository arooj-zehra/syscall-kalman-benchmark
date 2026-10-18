/*
 * IPC cases: pipes and shared memory.
 *
 *   plat_pipe             create a pipe (two handles)
 *   plat_pipe_write_read  write `size` bytes into a pipe and read them back in
 *                         the same process: one write call plus one read call.
 *                         Sizes stay below 4 KB, the default Windows pipe buffer,
 *                         so the write never blocks.
 *   plat_shm_create       create a named segment of `size` bytes and map it
 *   plat_shm_attach       attach to an existing segment (second mapping)
 *   plat_shm_detach       detach one mapping
 *   plat_shm_destroy      detach and remove the segment
 */

#include "bench.h"
#include "log.h"
#include "platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    size_t        size;
    plat_handle  *rd;
    plat_handle  *wr;
    plat_shm     *shm;
    plat_shm      owner;
    int           have_owner;
    char          base[256];
    unsigned char *wbuf;
    unsigned char *rbuf;
    plat_handle   pr;
    plat_handle   pw;
} ictx;

static ictx *ictx_new(const bench_case *c)
{
    ictx *x = (ictx *)calloc(1, sizeof *x);

    if (x == NULL) {
        return NULL;
    }
    x->size = c->size;
    x->rd = (plat_handle *)calloc((size_t)c->batch, sizeof *x->rd);
    x->wr = (plat_handle *)calloc((size_t)c->batch, sizeof *x->wr);
    x->shm = (plat_shm *)calloc((size_t)c->batch, sizeof *x->shm);
    x->wbuf = (unsigned char *)malloc(c->size > 0 ? c->size : 1);
    x->rbuf = (unsigned char *)malloc(c->size > 0 ? c->size : 1);
    x->pr = x->pw = PLAT_INVALID_HANDLE;
    if (x->rd == NULL || x->wr == NULL || x->shm == NULL || x->wbuf == NULL || x->rbuf == NULL) {
        free(x->rd);
        free(x->wr);
        free(x->shm);
        free(x->wbuf);
        free(x->rbuf);
        free(x);
        return NULL;
    }
    memset(x->wbuf, 0xa5, c->size > 0 ? c->size : 1);
    snprintf(x->base, sizeof x->base, "sk_bench_%lu", plat_getpid());
    return x;
}

static void ictx_free(ictx *x)
{
    if (x == NULL) {
        return;
    }
    if (x->have_owner) {
        (void)plat_shm_destroy(&x->owner);
    }
    if (x->pr != PLAT_INVALID_HANDLE) {
        (void)plat_close(x->pr);
    }
    if (x->pw != PLAT_INVALID_HANDLE) {
        (void)plat_close(x->pw);
    }
    free(x->rd);
    free(x->wr);
    free(x->shm);
    free(x->wbuf);
    free(x->rbuf);
    free(x);
}

static void seg_name(const ictx *x, int i, char *out, size_t n)
{
    snprintf(out, n, "%s_%d", x->base, i);
}

/* ---- pipes ----------------------------------------------------------------------- */

static int pipe_setup(void **ctx, const bench_case *c)
{
    ictx *x = ictx_new(c);

    if (x == NULL) {
        return -1;
    }
    *ctx = x;
    return 0;
}

static void ipc_teardown(void *ctx)
{
    ictx_free(ctx);
}

static int pipe_run(void *ctx, int i)
{
    ictx *x = ctx;

    return plat_pipe(&x->rd[i], &x->wr[i]);
}

static int pipe_cleanup(void *ctx, int n)
{
    ictx *x = ctx;
    int i;
    int rc = 0;

    for (i = 0; i < n; i++) {
        if (plat_close(x->rd[i]) != 0) {
            rc = -1;
        }
        if (plat_close(x->wr[i]) != 0) {
            rc = -1;
        }
    }
    return rc;
}

static int pipe_rw_setup(void **ctx, const bench_case *c)
{
    ictx *x = ictx_new(c);

    if (x == NULL) {
        return -1;
    }
    if (plat_pipe(&x->pr, &x->pw) != 0) {
        ictx_free(x);
        return -1;
    }
    *ctx = x;
    return 0;
}

static int pipe_rw_run(void *ctx, int i)
{
    ictx *x = ctx;
    size_t got = 0;

    (void)i;
    if (plat_write(x->pw, x->wbuf, x->size) != (int64_t)x->size) {
        return -1;
    }
    while (got < x->size) {
        int64_t n = plat_read(x->pr, x->rbuf + got, x->size - got);

        if (n <= 0) {
            return -1;
        }
        got += (size_t)n;
    }
    return 0;
}

/* ---- shared memory ----------------------------------------------------------------- */

static int shm_create_run(void *ctx, int i)
{
    ictx *x = ctx;
    char name[300];

    seg_name(x, i, name, sizeof name);
    return plat_shm_create(name, x->size, &x->shm[i]);
}

static int shm_destroy_all(void *ctx, int n)
{
    ictx *x = ctx;
    int i;
    int rc = 0;

    for (i = 0; i < n; i++) {
        if (x->shm[i].addr != NULL && plat_shm_destroy(&x->shm[i]) != 0) {
            rc = -1;
        }
    }
    return rc;
}

static int shm_destroy_run(void *ctx, int i)
{
    ictx *x = ctx;

    return plat_shm_destroy(&x->shm[i]);
}

static int shm_owner_setup(void **ctx, const bench_case *c)
{
    ictx *x = ictx_new(c);
    char name[300];

    if (x == NULL) {
        return -1;
    }
    seg_name(x, 9999, name, sizeof name);
    if (plat_shm_create(name, c->size, &x->owner) != 0) {
        ictx_free(x);
        return -1;
    }
    x->have_owner = 1;
    *ctx = x;
    return 0;
}

static int shm_attach_run(void *ctx, int i)
{
    ictx *x = ctx;
    char name[300];

    seg_name(x, 9999, name, sizeof name);
    return plat_shm_attach(name, x->size, &x->shm[i]);
}

static int shm_detach_all(void *ctx, int n)
{
    ictx *x = ctx;
    int i;
    int rc = 0;

    for (i = 0; i < n; i++) {
        if (x->shm[i].addr != NULL && plat_shm_detach(&x->shm[i]) != 0) {
            rc = -1;
        }
    }
    return rc;
}

static int shm_detach_run(void *ctx, int i)
{
    ictx *x = ctx;

    return plat_shm_detach(&x->shm[i]);
}

static int shm_create_prepare(void *ctx, int n)
{
    ictx *x = ctx;
    int i;

    for (i = 0; i < n; i++) {
        char name[300];

        seg_name(x, i, name, sizeof name);
        if (plat_shm_create(name, x->size, &x->shm[i]) != 0) {
            return -1;
        }
    }
    return 0;
}

static int shm_attach_prepare(void *ctx, int n)
{
    int i;

    for (i = 0; i < n; i++) {
        if (shm_attach_run(ctx, i) != 0) {
            return -1;
        }
    }
    return 0;
}

void bench_cases_ipc(bench_list *list)
{
    static const size_t pipe_sizes[] = {64u, 512u, 2048u};
    static const size_t shm_sizes[] = {4096u, 1048576u, 16777216u};
    bench_case c = {0};
    size_t k;

    c.group = "ipc";
    c.warmup = 3;
    c.teardown = ipc_teardown;

    c.call = "plat_pipe";
    c.note = "create a pipe";
    c.batch = 100;
    c.samples = 50;
    c.setup = pipe_setup;
    c.run = pipe_run;
    c.cleanup = pipe_cleanup;
    bench_add(list, &c);

    for (k = 0; k < sizeof pipe_sizes / sizeof pipe_sizes[0]; k++) {
        c.size = pipe_sizes[k];
        c.call = "plat_pipe_write_read";
        c.note = "one write plus one read through a pipe, same process";
        c.batch = 1000;
        c.samples = 40;
        c.setup = pipe_rw_setup;
        c.prepare = NULL;
        c.run = pipe_rw_run;
        c.cleanup = NULL;
        bench_add(list, &c);
    }

    for (k = 0; k < sizeof shm_sizes / sizeof shm_sizes[0]; k++) {
        c.size = shm_sizes[k];
        c.batch = c.size >= 16777216u ? 16 : 32;
        c.samples = 40;
        c.setup = pipe_setup;

        c.call = "plat_shm_create";
        c.note = "create and map a named segment";
        c.prepare = NULL;
        c.run = shm_create_run;
        c.cleanup = shm_destroy_all;
        bench_add(list, &c);

        c.call = "plat_shm_destroy";
        c.note = "detach and remove a segment";
        c.prepare = shm_create_prepare;
        c.run = shm_destroy_run;
        c.cleanup = NULL;
        bench_add(list, &c);

        c.setup = shm_owner_setup;

        c.call = "plat_shm_attach";
        c.note = "attach to an existing segment";
        c.prepare = NULL;
        c.run = shm_attach_run;
        c.cleanup = shm_detach_all;
        bench_add(list, &c);

        c.call = "plat_shm_detach";
        c.note = "detach one mapping of a segment";
        c.prepare = shm_attach_prepare;
        c.run = shm_detach_run;
        c.cleanup = NULL;
        bench_add(list, &c);
    }
}
