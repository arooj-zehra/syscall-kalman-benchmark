/*
 * Process cases: plat_spawn and plat_wait (Linux fork+exec+waitpid, Windows
 * CreateProcess+WaitForSingleObject). The program started is child_helper in
 * its "exit 0" mode, so the child does no work of its own.
 *
 *   plat_spawn       time until the call returns. On Linux that includes the
 *                    child's exec (the status pipe confirms it); on Windows
 *                    CreateProcess returns once the process object exists.
 *   plat_wait        wait for a child that has already ended: the cost of the
 *                    call itself, not of the child. (Sleeps 100 ms per sample
 *                    in prepare, which is untimed but counts in the cpu columns
 *                    as idle time, not CPU.)
 *   plat_spawn_wait  the full round trip: start a child and wait for it to end.
 */

#include "bench.h"
#include "log.h"
#include "platform.h"

#include <stdio.h>
#include <stdlib.h>

typedef struct {
    plat_proc procs[16];
    char     *argv[4];
} pctx;

static int proc_setup(void **ctx, const bench_case *c)
{
    pctx *p = (pctx *)calloc(1, sizeof *p);

    (void)c;
    if (p == NULL) {
        return -1;
    }
    p->argv[0] = (char *)g_env.helper;
    p->argv[1] = "exit";
    p->argv[2] = "0";
    p->argv[3] = NULL;
    *ctx = p;
    return 0;
}

static void proc_teardown(void *ctx)
{
    free(ctx);
}

static int spawn_one(pctx *p, int i)
{
    return plat_spawn(g_env.helper, p->argv, NULL, 0, &p->procs[i]);
}

static int wait_one(pctx *p, int i)
{
    int code = -1;

    if (plat_wait(p->procs[i], &code) != 0) {
        return -1;
    }
    return code == 0 ? 0 : -1;
}

static int spawn_run(void *ctx, int i)
{
    return spawn_one(ctx, i);
}

static int spawn_cleanup(void *ctx, int n)
{
    int i;
    int rc = 0;

    for (i = 0; i < n; i++) {
        if (wait_one(ctx, i) != 0) {
            rc = -1;
        }
    }
    return rc;
}

static int wait_prepare(void *ctx, int n)
{
    int i;

    for (i = 0; i < n; i++) {
        if (spawn_one(ctx, i) != 0) {
            return -1;
        }
    }
    plat_sleep_ms(100); /* let every child finish before the timed waits */
    return 0;
}

static int wait_run(void *ctx, int i)
{
    return wait_one(ctx, i);
}

static int roundtrip_run(void *ctx, int i)
{
    (void)i;
    if (spawn_one(ctx, 0) != 0) {
        return -1;
    }
    return wait_one(ctx, 0);
}

void bench_cases_process(bench_list *list)
{
    bench_case c = {0};

    c.group = "process";
    c.warmup = 5;
    c.setup = proc_setup;
    c.teardown = proc_teardown;

    c.call = "plat_spawn";
    c.note = "start child_helper; returns when the child is running";
    c.batch = 1;
    c.samples = 100;
    c.run = spawn_run;
    c.cleanup = spawn_cleanup;
    bench_add(list, &c);

    c.call = "plat_wait";
    c.note = "wait for a child that already ended (call cost only)";
    c.batch = 4;
    c.samples = 40;
    c.warmup = 2;
    c.prepare = wait_prepare;
    c.run = wait_run;
    c.cleanup = NULL;
    bench_add(list, &c);

    c.call = "plat_spawn_wait";
    c.note = "start child_helper and wait for it to exit";
    c.batch = 1;
    c.samples = 100;
    c.warmup = 5;
    c.prepare = NULL;
    c.run = roundtrip_run;
    bench_add(list, &c);
}
