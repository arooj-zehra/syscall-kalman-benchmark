/*
 * Linux-only extra cases (group "linux"): fork() on its own. Windows has no
 * fork(), so these have no counterpart there; they show what plat_spawn builds on.
 *
 *   fork_wait         fork a child that exits at once, then waitpid it
 *   fork_wait_64mb    the same while the parent holds 64 MB of touched memory:
 *                     fork must copy the page tables, so the cost grows with the
 *                     parent's size. CreateProcess does not depend on it.
 */

#include "bench.h"
#include "log.h"
#include "platform.h"

#include <stdlib.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

typedef struct {
    void  *mem;
    size_t len;
} fctx;

static int fork_setup(void **ctx, const bench_case *c)
{
    fctx *f = (fctx *)calloc(1, sizeof *f);

    if (f == NULL) {
        return -1;
    }
    f->len = c->size;
    if (f->len > 0) {
        f->mem = mmap(NULL, f->len, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (f->mem == MAP_FAILED) {
            free(f);
            return -1;
        }
        (void)bench_touch_write(f->mem, f->len);
    }
    *ctx = f;
    return 0;
}

static void fork_teardown(void *ctx)
{
    fctx *f = ctx;

    if (f->len > 0) {
        munmap(f->mem, f->len);
    }
    free(f);
}

static int fork_run(void *ctx, int i)
{
    pid_t pid;
    int status = 0;

    (void)ctx;
    (void)i;
    pid = fork();
    if (pid < 0) {
        return -1;
    }
    if (pid == 0) {
        _exit(0);
    }
    return waitpid(pid, &status, 0) == pid && WIFEXITED(status) ? 0 : -1;
}

void bench_cases_platform(bench_list *list)
{
    bench_case c = {0};

    c.group = "linux";
    c.note = "fork + child _exit(0) + waitpid";
    c.batch = 1;
    c.samples = 100;
    c.warmup = 5;
    c.setup = fork_setup;
    c.teardown = fork_teardown;
    c.run = fork_run;

    c.call = "fork_wait";
    c.size = 0;
    bench_add(list, &c);

    c.call = "fork_wait_64mb";
    c.size = 64u << 20;
    bench_add(list, &c);
}
