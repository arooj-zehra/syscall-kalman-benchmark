/*
 * bench.h - types shared by the benchmark harness and its case files.
 *
 * A benchmark case measures ONE wrapper (for example plat_read at 64 KB). Its
 * work is split into three parts so that only the call under test is timed:
 *
 *   prepare(ctx, n)   untimed   get ready for n calls (open files, spawn helpers...)
 *   run(ctx, i)       TIMED     the single call under test, i = 0 .. n-1
 *   cleanup(ctx, n)   untimed   undo what the n calls did (close, unmap, delete...)
 *
 * One "sample" is one prepare / n x run / cleanup round; its time is the time of
 * the n run() calls divided by n. A case takes `samples` samples after `warmup`
 * discarded rounds, and the statistics are computed over the samples.
 *
 * Fast calls (tens of nanoseconds to microseconds) use a large batch so the
 * clock reads are amortised; slow calls (process creation) use batch = 1 and
 * more samples, and are timed one by one.
 */
#ifndef BENCH_H
#define BENCH_H

#include <stddef.h>
#include <stdint.h>

#define BENCH_MAX_CASES 256

struct bench_case;

typedef struct bench_case {
    const char *group;   /* baseline, fileio, process, memory, ipc, perms, linux */
    const char *call;    /* the wrapper being measured, e.g. "plat_read" */
    const char *note;    /* one line for the CSV/report: what exactly is timed */
    size_t      size;    /* bytes the call works on; 0 when not applicable */
    int         batch;   /* run() calls per timed sample */
    int         samples; /* timed samples */
    int         warmup;  /* discarded rounds before sampling */

    int  (*setup)(void **ctx, const struct bench_case *c);  /* once per process */
    int  (*prepare)(void *ctx, int n);                       /* may be NULL */
    int  (*run)(void *ctx, int i);                           /* the timed call */
    int  (*cleanup)(void *ctx, int n);                       /* may be NULL */
    void (*teardown)(void *ctx);                             /* may be NULL */
} bench_case;

typedef struct {
    bench_case items[BENCH_MAX_CASES];
    int        count;
} bench_list;

/* Add a copy of *c to the list. Returns -1 when the list is full. */
int bench_add(bench_list *list, const bench_case *c);

/* Each cases_*.c file contributes its cases through one of these. */
void bench_cases_baseline(bench_list *list);
void bench_cases_fileio(bench_list *list);
void bench_cases_process(bench_list *list);
void bench_cases_memory(bench_list *list);
void bench_cases_ipc(bench_list *list);
void bench_cases_perms(bench_list *list);
void bench_cases_platform(bench_list *list);  /* OS-specific extras (Linux: raw fork) */

/* Run-wide settings the cases may read (set once from the command line). */
typedef struct {
    const char *tmpdir;  /* where temporary files go, with a trailing separator or "" */
    const char *helper;  /* path of child_helper */
    int         quick;   /* 1 = smoke-test sizes, not for real measurements */
} bench_env;

extern bench_env g_env;

/* "<tmpdir>sk_bench_<pid>_<tag>" into buf. */
void bench_tmp_name(char *buf, size_t n, const char *tag);

/* Create (or replace) a file of exactly `size` bytes filled with a fixed pattern.
 * Returns 0 or -1. */
int bench_make_file(const char *path, size_t size);

/* Touch one byte in every 4 KB page of [p, p+len) and return a value that
 * depends on them, so the compiler cannot remove the loop. */
unsigned bench_touch_write(void *p, size_t len);
unsigned bench_touch_read(const void *p, size_t len);

/* ---- statistics ---------------------------------------------------------- */

typedef struct {
    double mean;
    double median;
    double p95;
    double stddev;  /* sample standard deviation (n - 1); 0 for a single value */
    double min;
    double max;
    int    n;
} bench_stats;

/* Compute statistics over v[0..n-1]. The array is sorted in place.
 * p95 uses linear interpolation between closest ranks. Returns -1 if n < 1. */
int bench_compute_stats(double *v, int n, bench_stats *out);

#endif /* BENCH_H */
