/*
 * Permission cases: plat_chmod, plat_chown, plat_umask, plus plat_stat_path.
 *
 * chmod alternates between 0600 and 0644 so every call really changes the mode.
 * chown gives the file to its current owner: the call is real but changes
 * nothing, so it works without privilege. On Windows chmod and chown rewrite the
 * file's security descriptor, which is far more work than changing a mode word,
 * and plat_umask only stores a number (there is no Win32 equivalent).
 */

#include "bench.h"
#include "log.h"
#include "platform.h"

#include <stdlib.h>

typedef struct {
    char path[512];
    long uid;
    long gid;
} pm;

static int pm_setup(void **ctx, const bench_case *c)
{
    pm *p = (pm *)calloc(1, sizeof *p);

    (void)c;
    if (p == NULL) {
        return -1;
    }
    bench_tmp_name(p->path, sizeof p->path, "perms.dat");
    if (bench_make_file(p->path, 4096) != 0) {
        free(p);
        return -1;
    }
    p->uid = plat_getuid();
    p->gid = plat_getgid();
    *ctx = p;
    return 0;
}

static void pm_teardown(void *ctx)
{
    pm *p = ctx;

    (void)plat_unlink(p->path);
    free(p);
}

static int chmod_run(void *ctx, int i)
{
    pm *p = ctx;

    return plat_chmod(p->path, (i & 1) ? 0600u : 0644u);
}

static int chown_run(void *ctx, int i)
{
    pm *p = ctx;

    (void)i;
    return plat_chown(p->path, p->uid, p->gid);
}

static int umask_run(void *ctx, int i)
{
    (void)ctx;
    (void)plat_umask((i & 1) ? 022u : 077u);
    return 0;
}

static int stat_run(void *ctx, int i)
{
    pm *p = ctx;
    plat_stat st;

    (void)i;
    return plat_stat_path(p->path, &st);
}

void bench_cases_perms(bench_list *list)
{
    bench_case c = {0};

    c.group = "perms";
    c.warmup = 3;
    c.setup = pm_setup;
    c.teardown = pm_teardown;
    c.samples = 40;

    c.call = "plat_chmod";
    c.note = "change the permission mode of a file";
    c.batch = 500;
    c.run = chmod_run;
    bench_add(list, &c);

    c.call = "plat_chown";
    c.note = "set the owner to the current owner (no change)";
    c.run = chown_run;
    bench_add(list, &c);

    c.call = "plat_stat_path";
    c.note = "helper: read size, mode and owner";
    c.run = stat_run;
    bench_add(list, &c);

    c.call = "plat_umask";
    c.note = "set the file-creation mask";
    c.batch = 100000;
    c.samples = 50;
    c.run = umask_run;
    bench_add(list, &c);
}
