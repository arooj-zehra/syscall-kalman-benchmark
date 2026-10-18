/*
 * bench_main.c - the benchmark driver.
 *
 *   bench --list
 *   bench --out results/linux_run1.csv --run 1 [--group fileio,ipc] [--filter read]
 *         [--tmpdir DIR] [--samples N] [--quick]
 *
 * Normal mode (parent): selects the cases, then starts a fresh copy of this
 * program for EACH case with plat_spawn() and waits for it. A fresh process per
 * case means the CPU time and peak memory it reports belong to that case alone
 * (peak memory is a high-water mark and could not be separated otherwise), and
 * one case cannot leave heap or cache state behind for the next.
 *
 * Case mode (child, started by the parent with --case): runs one case and
 * appends one row to the CSV file. Running it by hand is also fine.
 *
 * CSV columns:
 *   os,run,group,call,size,batch,samples,mean_ns,median_ns,p95_ns,stddev_ns,
 *   min_ns,max_ns,ops,cpu_user_us,cpu_sys_us,peak_kb,peak_delta_kb
 * Times are nanoseconds per call. cpu_* are the CPU time this process used inside
 * the timed blocks of the case (prepare and cleanup excluded); divide by `ops`
 * for a per-call figure. See docs/BENCHMARK_METHOD.md.
 */

#include "bench.h"
#include "log.h"
#include "platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef HELPER_NAME
#define HELPER_NAME "child_helper"
#endif

#define CSV_HEADER                                                                       \
    "os,run,group,call,size,batch,samples,mean_ns,median_ns,p95_ns,stddev_ns,min_ns,"   \
    "max_ns,ops,cpu_user_us,cpu_sys_us,peak_kb,peak_delta_kb\n"

typedef struct {
    const char *out;
    const char *groups;   /* comma list or NULL = all */
    const char *filter;   /* substring of the call name or NULL */
    const char *one_call; /* case mode: exact call name */
    size_t      one_size;
    int         have_size;
    int         run;
    int         samples;  /* 0 = per-case default */
    int         list;
    int         inproc;
} options;

/* ---- case lookup ---------------------------------------------------------- */

static void collect_all(bench_list *list)
{
    bench_cases_baseline(list);
    bench_cases_fileio(list);
    bench_cases_process(list);
    bench_cases_memory(list);
    bench_cases_ipc(list);
    bench_cases_perms(list);
    bench_cases_platform(list);
}

static int group_selected(const options *o, const char *group)
{
    const char *p;
    size_t len;

    if (o->groups == NULL || strcmp(o->groups, "all") == 0) {
        return 1;
    }
    len = strlen(group);
    for (p = o->groups; *p != '\0';) {
        const char *end = strchr(p, ',');
        size_t seg = end != NULL ? (size_t)(end - p) : strlen(p);

        if (seg == len && strncmp(p, group, len) == 0) {
            return 1;
        }
        if (end == NULL) {
            break;
        }
        p = end + 1;
    }
    return 0;
}

static int case_selected(const options *o, const bench_case *c)
{
    if (!group_selected(o, c->group)) {
        return 0;
    }
    if (o->filter != NULL && strstr(c->call, o->filter) == NULL) {
        return 0;
    }
    return 1;
}

/* ---- measuring one case --------------------------------------------------- */

/* CPU time spent inside the timed blocks only (not in prepare/cleanup). */
static uint64_t g_cpu_user_us;
static uint64_t g_cpu_sys_us;

static int run_round(const bench_case *c, void *ctx, double *per_call_ns, int count_cpu)
{
    int n = c->batch;
    int i;
    uint64_t t0;
    uint64_t t1;
    plat_usage ua;
    plat_usage ub;
    int have_cpu = 0;

    if (c->prepare != NULL && c->prepare(ctx, n) != 0) {
        LOG_ERROR("%s: prepare failed", c->call);
        return -1;
    }
    if (count_cpu) {
        have_cpu = plat_usage_self(&ua) == 0; /* read before the clock starts */
    }
    t0 = plat_now_ns();
    for (i = 0; i < n; i++) {
        if (c->run(ctx, i) != 0) {
            LOG_ERROR("%s: call %d of %d failed", c->call, i + 1, n);
            return -1;
        }
    }
    t1 = plat_now_ns();
    if (have_cpu && plat_usage_self(&ub) == 0) {
        g_cpu_user_us += ub.user_us - ua.user_us;
        g_cpu_sys_us += ub.sys_us - ua.sys_us;
    }
    if (c->cleanup != NULL && c->cleanup(ctx, n) != 0) {
        LOG_ERROR("%s: cleanup failed", c->call);
        return -1;
    }
    if (per_call_ns != NULL) {
        *per_call_ns = (double)(t1 - t0) / (double)n;
    }
    return 0;
}

static void format_time(double ns, char *buf, size_t n)
{
    if (ns < 1000.0) {
        snprintf(buf, n, "%.1f ns", ns);
    } else if (ns < 1e6) {
        snprintf(buf, n, "%.2f us", ns / 1e3);
    } else {
        snprintf(buf, n, "%.2f ms", ns / 1e6);
    }
}

static int measure_case(const options *o, const bench_case *orig)
{
    bench_case c = *orig;
    plat_usage u_start;
    plat_usage u_after;
    bench_stats st;
    double *v;
    void *ctx = NULL;
    int rc = -1;
    int i;
    FILE *f;
    char tmed[32];
    char tp95[32];

    if (plat_usage_self(&u_start) != 0) {
        memset(&u_start, 0, sizeof u_start);
    }

    if (g_env.quick) {
        c.samples = c.samples > 5 ? 5 : c.samples;
        c.warmup = 1;
        c.batch = c.batch >= 16 ? c.batch / 8 : c.batch;
    }
    if (o->samples > 0) {
        c.samples = o->samples;
    }

    v = (double *)malloc((size_t)c.samples * sizeof *v);
    if (v == NULL) {
        LOG_ERROR("out of memory for %d samples", c.samples);
        return -1;
    }

    if (c.setup != NULL && c.setup(&ctx, &c) != 0) {
        LOG_ERROR("%s: setup failed", c.call);
        free(v);
        return -1;
    }

    for (i = 0; i < c.warmup; i++) {
        if (run_round(&c, ctx, NULL, 0) != 0) {
            goto done;
        }
    }

    for (i = 0; i < c.samples; i++) {
        if (run_round(&c, ctx, &v[i], 1) != 0) {
            goto done;
        }
    }
    if (plat_usage_self(&u_after) != 0) {
        u_after = u_start;
    }

    if (bench_compute_stats(v, c.samples, &st) != 0) {
        goto done;
    }

    f = fopen(o->out, "a");
    if (f == NULL) {
        LOG_ERROR("cannot append to %s", o->out);
        goto done;
    }
    fprintf(f, "%s,%d,%s,%s,%llu,%d,%d,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%llu,%llu,%llu,%llu,%llu\n",
            plat_name(), o->run, c.group, c.call, (unsigned long long)c.size, c.batch,
            c.samples, st.mean, st.median, st.p95, st.stddev, st.min, st.max,
            (unsigned long long)c.batch * (unsigned long long)c.samples,
            (unsigned long long)g_cpu_user_us,
            (unsigned long long)g_cpu_sys_us,
            (unsigned long long)u_after.peak_kb,
            (unsigned long long)(u_after.peak_kb - u_start.peak_kb));
    fclose(f);

    format_time(st.median, tmed, sizeof tmed);
    format_time(st.p95, tp95, sizeof tp95);
    printf("    median %s  p95 %s  (%d samples x %d calls)\n", tmed, tp95, c.samples, c.batch);
    fflush(stdout);
    rc = 0;

done:
    if (c.teardown != NULL) {
        c.teardown(ctx);
    }
    free(v);
    return rc;
}

/* ---- command line ----------------------------------------------------------- */

static void usage(void)
{
    printf("usage: bench [options]\n"
           "  --list               list the cases and exit\n"
           "  --out FILE           CSV file to append to (required to measure)\n"
           "  --run N              repeat number written into the CSV (default 1)\n"
           "  --group a,b          only these groups: baseline fileio process memory ipc perms linux\n"
           "  --filter TEXT        only cases whose call name contains TEXT\n"
           "  --tmpdir DIR         directory for temporary files (default: current directory)\n"
           "  --samples N          override the number of samples per case\n"
           "  --quick              tiny sizes for a smoke test; NOT for real measurements\n"
           "  --inproc             run all cases in this process (peak memory is then cumulative)\n"
           "  --helper PATH        path of child_helper (default: next to this program)\n"
           "  --case CALL --size N run a single case (used internally)\n");
}

static int parse_args(int argc, char **argv, options *o)
{
    int i;

    for (i = 1; i < argc; i++) {
        const char *a = argv[i];
        int has_value = i + 1 < argc;

        if (strcmp(a, "--list") == 0) {
            o->list = 1;
        } else if (strcmp(a, "--quick") == 0) {
            g_env.quick = 1;
        } else if (strcmp(a, "--inproc") == 0) {
            o->inproc = 1;
        } else if (strcmp(a, "--help") == 0 || strcmp(a, "-h") == 0) {
            usage();
            return 1;
        } else if (!has_value) {
            fprintf(stderr, "bench: option %s needs a value\n", a);
            return -1;
        } else if (strcmp(a, "--out") == 0) {
            o->out = argv[++i];
        } else if (strcmp(a, "--run") == 0) {
            o->run = atoi(argv[++i]);
        } else if (strcmp(a, "--group") == 0) {
            o->groups = argv[++i];
        } else if (strcmp(a, "--filter") == 0) {
            o->filter = argv[++i];
        } else if (strcmp(a, "--tmpdir") == 0) {
            g_env.tmpdir = argv[++i];
        } else if (strcmp(a, "--samples") == 0) {
            o->samples = atoi(argv[++i]);
        } else if (strcmp(a, "--helper") == 0) {
            g_env.helper = argv[++i];
        } else if (strcmp(a, "--case") == 0) {
            o->one_call = argv[++i];
        } else if (strcmp(a, "--size") == 0) {
            o->one_size = (size_t)strtoull(argv[++i], NULL, 10);
            o->have_size = 1;
        } else {
            fprintf(stderr, "bench: unknown option %s (try --help)\n", a);
            return -1;
        }
    }
    return 0;
}

/* The tmpdir must end in a separator so names can be appended directly. */
static void fix_tmpdir(char *store, size_t n)
{
    size_t len = strlen(g_env.tmpdir);

    if (len == 0) {
        return;
    }
    if (g_env.tmpdir[len - 1] == '/') {
        return;
    }
    /* Always end in '/': Windows accepts it, and a trailing backslash would
     * escape the closing quote when the path is passed to a child process. */
    if (g_env.tmpdir[len - 1] == '\\') {
        snprintf(store, n, "%.*s/", (int)(len - 1), g_env.tmpdir);
    } else {
        snprintf(store, n, "%s/", g_env.tmpdir);
    }
    g_env.tmpdir = store;
}

static void default_helper(char *store, size_t n)
{
    char exe[1024];
    char *fwd;
    char *back;
    char *sep;

    if (plat_exe_path(exe, sizeof exe) != 0) {
        snprintf(store, n, "./%s", HELPER_NAME);
        return;
    }
    fwd = strrchr(exe, '/');
    back = strrchr(exe, '\\');
    sep = (fwd != NULL && back != NULL) ? (fwd > back ? fwd : back) : (fwd != NULL ? fwd : back);
    if (sep == NULL) {
        snprintf(store, n, "./%s", HELPER_NAME);
        return;
    }
    snprintf(store, n, "%.*s%s", (int)(sep - exe) + 1, exe, HELPER_NAME);
}

/* Write the CSV header (and a .meta.txt describing the machine) for a new file. */
static int prepare_output(const options *o)
{
    FILE *f;
    char meta_path[1100];
    char text[256];
    char stamp[40];
    int existing = 0;

    f = fopen(o->out, "rb");
    if (f != NULL) {
        existing = fgetc(f) != EOF;
        fclose(f);
    }
    if (existing) {
        return 0; /* appending to an existing file: keep its header */
    }
    f = fopen(o->out, "w");
    if (f == NULL) {
        fprintf(stderr, "bench: cannot create %s\n", o->out);
        return -1;
    }
    fputs(CSV_HEADER, f);
    fclose(f);

    snprintf(meta_path, sizeof meta_path, "%s.meta.txt", o->out);
    f = fopen(meta_path, "w");
    if (f == NULL) {
        return 0; /* not fatal */
    }
    plat_env_summary(text, sizeof text);
    plat_timestamp(stamp, sizeof stamp);
    fprintf(f, "started:   %s\n", stamp);
    fprintf(f, "platform:  %s\n", plat_name());
    fprintf(f, "os:        %s\n", text);
    fprintf(f, "cpus:      %d logical\n", plat_cpu_count());
    fprintf(f, "build:     %s\n", BUILD_TYPE);
#if defined(__VERSION__)
    fprintf(f, "compiler:  %s\n", __VERSION__);
#endif
    fprintf(f, "tmpdir:    %s\n", g_env.tmpdir[0] != '\0' ? g_env.tmpdir : "(current directory)");
    fprintf(f, "quick:     %s\n", g_env.quick ? "yes (smoke test, do not use)" : "no");
    fclose(f);
    return 0;
}

/* ---- parent: one fresh process per case ---------------------------------------- */

static int run_case_in_child(const options *o, const bench_case *c, const char *exe)
{
    char size_s[32];
    char run_s[16];
    char samples_s[16];
    char *args[32];
    int na = 0;
    plat_proc p;
    int code = -1;

    snprintf(size_s, sizeof size_s, "%llu", (unsigned long long)c->size);
    snprintf(run_s, sizeof run_s, "%d", o->run);
    snprintf(samples_s, sizeof samples_s, "%d", o->samples);

    args[na++] = (char *)exe;
    args[na++] = "--case";
    args[na++] = (char *)c->call;
    args[na++] = "--size";
    args[na++] = size_s;
    args[na++] = "--out";
    args[na++] = (char *)o->out;
    args[na++] = "--run";
    args[na++] = run_s;
    args[na++] = "--helper";
    args[na++] = (char *)g_env.helper;
    if (g_env.tmpdir[0] != '\0') {
        args[na++] = "--tmpdir";
        args[na++] = (char *)g_env.tmpdir;
    }
    if (o->samples > 0) {
        args[na++] = "--samples";
        args[na++] = samples_s;
    }
    if (g_env.quick) {
        args[na++] = "--quick";
    }
    args[na] = NULL;

    if (plat_spawn(exe, args, NULL, 0, &p) != 0) {
        return -1;
    }
    if (plat_wait(p, &code) != 0) {
        return -1;
    }
    return code == 0 ? 0 : -1;
}

int main(int argc, char **argv)
{
    options o;
    bench_list *list;
    char tmp_store[1024];
    char helper_store[1100];
    char exe[1024];
    int rc;
    int i;
    int selected = 0;
    int done = 0;
    int failed = 0;

    memset(&o, 0, sizeof o);
    o.run = 1;

    log_init(NULL);
    rc = parse_args(argc, argv, &o);
    if (rc != 0) {
        return rc > 0 ? 0 : 2;
    }
    fix_tmpdir(tmp_store, sizeof tmp_store);
    if (g_env.helper[0] == '\0') {
        default_helper(helper_store, sizeof helper_store);
        g_env.helper = helper_store;
    }

    list = (bench_list *)calloc(1, sizeof *list);
    if (list == NULL) {
        return 2;
    }
    collect_all(list);

    if (o.list) {
        printf("%-9s %-24s %10s %7s %8s\n", "group", "call", "size", "batch", "samples");
        for (i = 0; i < list->count; i++) {
            const bench_case *c = &list->items[i];
            if (case_selected(&o, c)) {
                printf("%-9s %-24s %10llu %7d %8d\n", c->group, c->call,
                       (unsigned long long)c->size, c->batch, c->samples);
            }
        }
        free(list);
        return 0;
    }

    if (o.out == NULL) {
        fprintf(stderr, "bench: --out FILE is required (see --help)\n");
        free(list);
        return 2;
    }

    /* Case mode: run exactly one case. */
    if (o.one_call != NULL) {
        for (i = 0; i < list->count; i++) {
            const bench_case *c = &list->items[i];
            if (strcmp(c->call, o.one_call) == 0 && (!o.have_size || c->size == o.one_size)) {
                int r = measure_case(&o, c);
                free(list);
                return r == 0 ? 0 : 1;
            }
        }
        fprintf(stderr, "bench: no case %s with size %llu\n", o.one_call,
                (unsigned long long)o.one_size);
        free(list);
        return 2;
    }

    /* Parent mode. */
    if (prepare_output(&o) != 0) {
        free(list);
        return 2;
    }
    for (i = 0; i < list->count; i++) {
        selected += case_selected(&o, &list->items[i]);
    }
    if (plat_exe_path(exe, sizeof exe) != 0) {
        snprintf(exe, sizeof exe, "%s", argv[0]);
    }

    {
        char env[256];
        plat_env_summary(env, sizeof env);
        printf("bench: %d cases, run %d, %s\n", selected, o.run, env);
    }
    for (i = 0; i < list->count; i++) {
        const bench_case *c = &list->items[i];
        int r;

        if (!case_selected(&o, c)) {
            continue;
        }
        done++;
        printf("[%3d/%d] %-9s %-24s size %llu\n", done, selected, c->group, c->call,
               (unsigned long long)c->size);
        fflush(stdout);
        r = o.inproc ? measure_case(&o, c) : run_case_in_child(&o, c, exe);
        if (r != 0) {
            printf("    FAILED\n");
            failed++;
        }
    }
    printf("bench: finished, %d of %d cases ok, results in %s\n", selected - failed, selected, o.out);
    free(list);
    return failed == 0 ? 0 : 1;
}
