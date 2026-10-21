#include "kfproto.h"

#include "log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef KF_EXE_SUFFIX
#define KF_EXE_SUFFIX ""
#endif

int kf_msg_send(plat_handle h, const kf_msg *m)
{
    const unsigned char *p = (const unsigned char *)m;
    size_t left = sizeof *m;

    while (left > 0) {
        int64_t n = plat_write(h, p, left);

        if (n <= 0) {
            return -1;
        }
        p += n;
        left -= (size_t)n;
    }
    return 0;
}

int kf_msg_recv(plat_handle h, kf_msg *m)
{
    unsigned char *p = (unsigned char *)m;
    size_t got = 0;

    while (got < sizeof *m) {
        int64_t n = plat_read(h, p + got, sizeof *m - got);

        if (n < 0) {
            return -1;
        }
        if (n == 0) {
            return got == 0 ? 0 : -1;
        }
        got += (size_t)n;
    }
    return 1;
}

void kf_split_tracks(uint64_t tracks, int workers, int index, uint64_t *first, uint64_t *count)
{
    uint64_t w = (uint64_t)workers;
    uint64_t i = (uint64_t)index;
    uint64_t base = tracks / w;
    uint64_t extra = tracks % w;

    *count = base + (i < extra ? 1 : 0);
    *first = i * base + (i < extra ? i : extra);
}

int kf_sibling_path(const char *name, char *out, size_t n)
{
    char exe[1024];
    char *fwd;
    char *back;
    char *sep;

    if (plat_exe_path(exe, sizeof exe) != 0) {
        return -1;
    }
    fwd = strrchr(exe, '/');
    back = strrchr(exe, '\\');
    sep = (fwd != NULL && back != NULL) ? (fwd > back ? fwd : back) : (fwd != NULL ? fwd : back);
    if (sep == NULL) {
        snprintf(out, n, "%s%s", name, KF_EXE_SUFFIX);
    } else {
        snprintf(out, n, "%.*s%s%s", (int)(sep - exe) + 1, exe, name, KF_EXE_SUFFIX);
    }
    return 0;
}

static int cmp_double(const void *a, const void *b)
{
    double x = *(const double *)a;
    double y = *(const double *)b;

    return (x > y) - (x < y);
}

double kf_median(double *v, int n)
{
    if (n < 1) {
        return 0.0;
    }
    qsort(v, (size_t)n, sizeof v[0], cmp_double);
    return (n % 2) ? v[n / 2] : 0.5 * (v[n / 2 - 1] + v[n / 2]);
}

#define KF_CSV_HEADER                                                                              \
    "os,program,workers,io,tracks,steps,startup_ms,load_ms,compute_ms,compute_first_ms,hash_ms,"  \
    "write_ms,total_ms,rounds,cpu_ms,peak_kb,master_peak_kb,est_hash,verified"

int kf_stats_append_csv(const char *path, const kf_run_stats *s)
{
    FILE *f = fopen(path, "rb");
    int exists = 0;

    if (f != NULL) {
        char line[512];

        if (fgets(line, sizeof line, f) != NULL) {
            exists = 1;
            line[strcspn(line, "\r\n")] = '\0';
            if (strcmp(line, KF_CSV_HEADER) != 0) {
                fclose(f);
                LOG_ERROR("%s has an older column layout; move or delete it and run again", path);
                return -1;
            }
        }
        fclose(f);
    }
    f = fopen(path, "a");
    if (f == NULL) {
        LOG_ERROR("cannot append to %s", path);
        return -1;
    }
    if (!exists) {
        fputs(KF_CSV_HEADER "\n", f);
    }
    fprintf(f, "%s,%s,%d,%s,%llu,%llu,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%d,%.3f,%llu,%llu,0x%016llx,%s\n",
            plat_name(), s->program, s->workers, s->io, (unsigned long long)s->tracks,
            (unsigned long long)s->steps, s->startup_ms, s->load_ms, s->compute_ms,
            s->compute_first_ms, s->hash_ms, s->write_ms, s->total_ms, s->rounds, s->cpu_ms,
            (unsigned long long)s->peak_kb, (unsigned long long)s->master_peak_kb,
            (unsigned long long)s->hash, s->verified);
    fclose(f);
    return 0;
}
