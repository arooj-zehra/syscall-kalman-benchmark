/*
 * Baseline cases: what the measurement itself costs. Read these first; every
 * other number sits on top of them.
 *   empty_call     function-pointer call + loop in the harness (no system call)
 *   plat_now_ns    one clock read (clock_gettime / QueryPerformanceCounter)
 *   plat_getpid    the cheapest real call: getpid() on Linux. On Windows
 *                  GetCurrentProcessId() reads the thread block and does not
 *                  enter the kernel, so it is a floor, not a system call.
 */

#include "bench.h"
#include "platform.h"

static volatile uint64_t g_sink;

static int run_empty(void *ctx, int i)
{
    (void)ctx;
    g_sink = (uint64_t)i;
    return 0;
}

static int run_now(void *ctx, int i)
{
    (void)ctx;
    (void)i;
    g_sink = plat_now_ns();
    return 0;
}

static int run_getpid(void *ctx, int i)
{
    (void)ctx;
    (void)i;
    g_sink = plat_getpid();
    return 0;
}

void bench_cases_baseline(bench_list *list)
{
    bench_case c = {0};

    c.group = "baseline";
    c.batch = 100000;
    c.samples = 50;
    c.warmup = 3;

    c.call = "empty_call";
    c.note = "harness loop overhead, no system call";
    c.run = run_empty;
    bench_add(list, &c);

    c.call = "plat_now_ns";
    c.note = "one monotonic clock read";
    c.run = run_now;
    bench_add(list, &c);

    c.call = "plat_getpid";
    c.note = "getpid() / GetCurrentProcessId()";
    c.run = run_getpid;
    bench_add(list, &c);
}
