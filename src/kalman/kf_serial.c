/*
 * kf_serial - the serial reference: one process runs the filter over every track.
 *
 *   kf_serial --in data/meas.bin [--out data/est_serial.bin] [--io mmap|read]
 *             [--verify-input] [--csv results/serial.csv]
 *
 *   --io mmap   map the input file and use it in place (default)
 *   --io read   read the whole input into memory with plat_read first
 *   --out       write the estimates (streamed track by track); omit to skip the write
 *   --csv       append one timing row (used to compute the Phase 6 speedup)
 *
 * With --io mmap the pages are fetched from the file cache on first use, so that
 * time lands in "compute"; with --io read it lands in "load".
 * Prints "est_hash=0x..." - a parallel run must print the same value.
 */

#include "kalman.h"
#include "kfio.h"
#include "log.h"
#include "platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int usage(void)
{
    fprintf(stderr, "usage: kf_serial --in FILE [--out FILE] [--io mmap|read] [--verify-input] [--csv FILE]\n");
    return 2;
}

static void append_csv(const char *path, const char *io, const kf_file_header *h, double load_ms,
                       double compute_ms, double write_ms, double total_ms, uint64_t hash)
{
    FILE *f = fopen(path, "rb");
    int exists = 0;

    if (f != NULL) {
        exists = fgetc(f) != EOF;
        fclose(f);
    }
    f = fopen(path, "a");
    if (f == NULL) {
        LOG_ERROR("cannot append to %s", path);
        return;
    }
    if (!exists) {
        fputs("os,program,workers,io,tracks,steps,load_ms,compute_ms,write_ms,total_ms,est_hash\n", f);
    }
    fprintf(f, "%s,serial,1,%s,%llu,%llu,%.3f,%.3f,%.3f,%.3f,0x%016llx\n", plat_name(), io,
            (unsigned long long)h->f.n_tracks, (unsigned long long)h->f.n_steps, load_ms, compute_ms,
            write_ms, total_ms, (unsigned long long)hash);
    fclose(f);
}

int main(int argc, char **argv)
{
    const char *in = NULL;
    const char *out = NULL;
    const char *csv = NULL;
    int use_mmap = 1;
    int verify = 0;
    kf_reader r;
    kf_writer w;
    kf_model model;
    kf_gen_params p;
    kf_file_header eh;
    double *est;
    uint64_t t_start;
    uint64_t t_loaded;
    uint64_t t_compute = 0;
    uint64_t t_write = 0;
    uint64_t t_end;
    uint64_t hash = KF_HASH_INIT;
    uint64_t n_tracks;
    uint64_t n_steps;
    uint64_t t;
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
    kf_header_params(&r.hdr, &p);
    kf_model_init(&model, p.dt, p.sigma_a, p.sigma_r, p.sigma_p0, p.sigma_v0);

    est = (double *)malloc((size_t)n_steps * 4 * sizeof *est);
    if (est == NULL) {
        LOG_ERROR("out of memory");
        kf_reader_close(&r);
        return 1;
    }
    if (out != NULL) {
        kf_header_init(&eh, KF_MAGIC_EST, 4, n_tracks, n_steps, &p);
        if (kf_writer_open(&w, out, &eh) != 0) {
            free(est);
            kf_reader_close(&r);
            return 1;
        }
    }

    for (t = 0; t < n_tracks; t++) {
        uint64_t a0 = plat_now_ns();
        uint64_t a1;

        kf_run_track(&model, r.data + t * n_steps * 2, (size_t)n_steps, est);
        a1 = plat_now_ns();
        t_compute += a1 - a0;
        hash = kf_hash_update(hash, est, (size_t)n_steps * 4 * sizeof(double));
        if (out != NULL) {
            if (kf_writer_append(&w, est, (size_t)n_steps * 4) != 0) {
                free(est);
                kf_reader_close(&r);
                return 1;
            }
            t_write += plat_now_ns() - a1;
        }
    }
    if (out != NULL) {
        uint64_t a0 = plat_now_ns();

        if (kf_writer_close(&w) != 0) {
            free(est);
            kf_reader_close(&r);
            return 1;
        }
        t_write += plat_now_ns() - a0;
    }
    t_end = plat_now_ns();

    {
        double load_ms = (double)(t_loaded - t_start) / 1e6;
        double comp_ms = (double)t_compute / 1e6;
        double write_ms = (double)t_write / 1e6;
        double total_ms = (double)(t_end - t_start) / 1e6;
        double steps_total = (double)n_tracks * (double)n_steps;

        printf("kf_serial: %llu tracks x %llu steps, io=%s\n", (unsigned long long)n_tracks,
               (unsigned long long)n_steps, use_mmap ? "mmap" : "read");
        printf("  load    : %9.2f ms\n", load_ms);
        printf("  compute : %9.2f ms  (%.2f million steps/s)\n", comp_ms, steps_total / comp_ms / 1e3);
        printf("  write   : %9.2f ms%s\n", write_ms, out != NULL ? "" : "  (skipped)");
        printf("  total   : %9.2f ms\n", total_ms);
        printf("est_hash=0x%016llx\n", (unsigned long long)hash);
        if (csv != NULL) {
            append_csv(csv, use_mmap ? "mmap" : "read", &r.hdr, load_ms, comp_ms, write_ms, total_ms, hash);
        }
    }

    free(est);
    kf_reader_close(&r);
    return 0;
}
