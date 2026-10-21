/*
 * kf_serial - the serial reference: one process runs the filter over every track.
 *
 *   kf_serial --in data/meas.bin [--out data/est_serial.bin] [--io mmap|read]
 *             [--repeat R] [--verify-input] [--csv results/serial.csv]
 *
 *   --io mmap   map the input file and use it in place (default)
 *   --io read   read the whole input into memory with plat_read first
 *   --repeat R  run the filter R times over the same buffers; compute_ms is the
 *               median of the R passes, compute_first_ms the first one (which
 *               also pays for touching every page of the output for the first
 *               time). Default 1.
 *   --out       write the estimates to a file; omit to skip the write
 *   --csv       append one timing row (the baseline for the parallel speedup)
 *
 * Like the parallel master, the results of all tracks go into one output buffer
 * (so the memory behaviour is the same), which is then hashed and optionally
 * written. Prints "est_hash=0x..." - a parallel run must print the same value.
 *
 * With --io mmap the input pages are fetched from the file cache on first use,
 * so that time lands in the first compute pass; with --io read it lands in "load".
 * --verify-input hashes the input first; it is a check, not a measurement, so do
 * not combine it with --csv timings.
 */

#include "kalman.h"
#include "kfio.h"
#include "kfproto.h"
#include "log.h"
#include "platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int usage(void)
{
    fprintf(stderr, "usage: kf_serial --in FILE [--out FILE] [--io mmap|read] [--repeat R] "
                    "[--verify-input] [--csv FILE]\n");
    return 2;
}

static double ms(uint64_t ns)
{
    return (double)ns / 1e6;
}

int main(int argc, char **argv)
{
    const char *in = NULL;
    const char *out = NULL;
    const char *csv = NULL;
    int use_mmap = 1;
    int verify = 0;
    int repeat = 1;
    kf_reader r;
    kf_model model;
    kf_gen_params p;
    kf_run_stats st;
    double *est = NULL;
    double *times = NULL;
    uint64_t t_start;
    uint64_t t_loaded;
    uint64_t t_hash;
    uint64_t t_write = 0;
    uint64_t hash;
    uint64_t n_tracks;
    uint64_t n_steps;
    size_t per_track;
    plat_usage u0;
    plat_usage u1;
    int rc = 1;
    int i;

    log_init(NULL);
    for (i = 1; i < argc; i++) {
        const char *a = argv[i];

        if (strcmp(a, "--verify-input") == 0) {
            verify = 1;
        } else if (i + 1 >= argc) {
            return usage();
        } else if (strcmp(a, "--in") == 0) {
            in = argv[++i];
        } else if (strcmp(a, "--out") == 0) {
            out = argv[++i];
        } else if (strcmp(a, "--csv") == 0) {
            csv = argv[++i];
        } else if (strcmp(a, "--repeat") == 0) {
            repeat = atoi(argv[++i]);
            if (repeat < 1 || repeat > 1000) {
                return usage();
            }
        } else if (strcmp(a, "--io") == 0) {
            const char *v = argv[++i];

            if (strcmp(v, "mmap") == 0) {
                use_mmap = 1;
            } else if (strcmp(v, "read") == 0) {
                use_mmap = 0;
            } else {
                return usage();
            }
        } else {
            return usage();
        }
    }
    if (in == NULL) {
        return usage();
    }

    t_start = plat_now_ns();
    if (kf_reader_open(&r, in, KF_MAGIC_MEAS, use_mmap) != 0) {
        return 1;
    }
    if (verify) {
        uint64_t h = kf_reader_hash(&r);

        if (h != r.hdr.f.data_hash) {
            LOG_ERROR("%s: data hash 0x%016llx does not match the header 0x%016llx", in,
                      (unsigned long long)h, (unsigned long long)r.hdr.f.data_hash);
            kf_reader_close(&r);
            return 1;
        }
        printf("input hash verified: 0x%016llx\n", (unsigned long long)h);
    }
    t_loaded = plat_now_ns();

    n_tracks = r.hdr.f.n_tracks;
    n_steps = r.hdr.f.n_steps;
    per_track = (size_t)n_steps * 4;
    kf_header_params(&r.hdr, &p);
    kf_model_init(&model, p.dt, p.sigma_a, p.sigma_r, p.sigma_p0, p.sigma_v0);

    /* Anonymous memory, like the parallel version's shared segment: zero-filled,
     * and every page is faulted in on first write. */
    est = (double *)plat_map_anon((size_t)n_tracks * per_track * sizeof *est);
    times = (double *)malloc((size_t)repeat * sizeof *times);
    if (est == NULL || times == NULL) {
        LOG_ERROR("out of memory");
        goto done;
    }

    memset(&st, 0, sizeof st);
    (void)plat_usage_self(&u0);
    for (i = 0; i < repeat; i++) {
        uint64_t a0 = plat_now_ns();
        uint64_t t;

        for (t = 0; t < n_tracks; t++) {
            kf_run_track(&model, r.data + t * n_steps * 2, (size_t)n_steps, est + t * per_track);
        }
        times[i] = ms(plat_now_ns() - a0);
    }
    (void)plat_usage_self(&u1);
    st.compute_first_ms = times[0];
    st.compute_ms = kf_median(times, repeat);

    {
        uint64_t a0 = plat_now_ns();

        hash = kf_hash_update(KF_HASH_INIT, est, (size_t)n_tracks * per_track * sizeof *est);
        t_hash = plat_now_ns() - a0;
    }

    if (out != NULL) {
        kf_file_header eh;
        kf_writer w;
        uint64_t a0 = plat_now_ns();
        uint64_t t;

        kf_header_init(&eh, KF_MAGIC_EST, 4, n_tracks, n_steps, &p);
        if (kf_writer_open(&w, out, &eh) != 0) {
            goto done;
        }
        for (t = 0; t < n_tracks; t++) {
            if (kf_writer_append_nohash(&w, est + t * per_track, per_track) != 0) {
                goto done;
            }
        }
        kf_writer_set_hash(&w, hash);
        if (kf_writer_close(&w) != 0) {
            goto done;
        }
        t_write = plat_now_ns() - a0;
    }

    st.program = "serial";
    st.workers = 1;
    st.io = use_mmap ? "mmap" : "read";
    st.tracks = n_tracks;
    st.steps = n_steps;
    st.load_ms = ms(t_loaded - t_start);
    st.hash_ms = ms(t_hash);
    st.write_ms = ms(t_write);
    st.total_ms = ms(plat_now_ns() - t_start);
    st.rounds = repeat;
    st.cpu_ms = (double)((u1.user_us + u1.sys_us) - (u0.user_us + u0.sys_us)) / 1e3 / repeat;
    st.peak_kb = u1.peak_kb;
    st.hash = hash;
    st.verified = "ref";

    {
        double steps_total = (double)n_tracks * (double)n_steps;

        printf("kf_serial: %llu tracks x %llu steps, io=%s, %d pass%s\n", (unsigned long long)n_tracks,
               (unsigned long long)n_steps, st.io, repeat, repeat == 1 ? "" : "es");
        printf("  load    : %9.2f ms\n", st.load_ms);
        printf("  compute : %9.2f ms  (%.2f million steps/s)%s\n", st.compute_ms,
               steps_total / st.compute_ms / 1e3, repeat > 1 ? "  median of passes" : "");
        if (repeat > 1) {
            printf("  1st pass: %9.2f ms\n", st.compute_first_ms);
        }
        printf("  hash    : %9.2f ms\n", st.hash_ms);
        printf("  write   : %9.2f ms%s\n", st.write_ms, out != NULL ? "" : "  (skipped)");
        printf("  total   : %9.2f ms\n", st.total_ms);
        printf("est_hash=0x%016llx\n", (unsigned long long)hash);
    }
    if (csv != NULL && kf_stats_append_csv(csv, &st) != 0) {
        goto done;
    }
    rc = 0;

done:
    free(times);
    if (est != NULL) {
        (void)plat_unmap_anon(est, (size_t)n_tracks * per_track * sizeof *est);
    }
    kf_reader_close(&r);
    return rc;
}
