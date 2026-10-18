/*
 * kfgen - write a synthetic measurement file.
 *
 *   kfgen --out data/meas.bin [--preset small|medium|large]
 *         [--tracks N] [--steps M] [--seed S]
 *         [--dt X] [--sigma-a X] [--sigma-r X] [--sigma-p0 X] [--sigma-v0 X]
 *
 * Presets: small 16 x 2,000 (0.5 MiB), medium 128 x 20,000 (39 MiB),
 *          large 256 x 100,000 (391 MiB). Default: medium.
 * Prints "data_hash=0x..." so two runs (or two operating systems) can be compared.
 */

#include "kfio.h"
#include "log.h"
#include "platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int usage(void)
{
    fprintf(stderr,
            "usage: kfgen --out FILE [--preset small|medium|large] [--tracks N] [--steps M]\n"
            "             [--seed S] [--dt X] [--sigma-a X] [--sigma-r X] [--sigma-p0 X] [--sigma-v0 X]\n");
    return 2;
}

int main(int argc, char **argv)
{
    const char *out = NULL;
    uint64_t tracks = 128;
    uint64_t steps = 20000;
    kf_gen_params p;
    kf_file_header hdr;
    kf_writer w;
    double *buf;
    uint64_t t;
    uint64_t t0;
    uint64_t t1;
    int i;

    kf_gen_defaults(&p);
    log_init(NULL);

    for (i = 1; i < argc; i++) {
        const char *a = argv[i];

        if (i + 1 >= argc) {
            return usage();
        }
        if (strcmp(a, "--out") == 0) {
            out = argv[++i];
        } else if (strcmp(a, "--preset") == 0) {
            const char *v = argv[++i];

            if (strcmp(v, "small") == 0) {
                tracks = 16;
                steps = 2000;
            } else if (strcmp(v, "medium") == 0) {
                tracks = 128;
                steps = 20000;
            } else if (strcmp(v, "large") == 0) {
                tracks = 256;
                steps = 100000;
            } else {
                return usage();
            }
        } else if (strcmp(a, "--tracks") == 0) {
            tracks = strtoull(argv[++i], NULL, 10);
        } else if (strcmp(a, "--steps") == 0) {
            steps = strtoull(argv[++i], NULL, 10);
        } else if (strcmp(a, "--seed") == 0) {
            p.seed = strtoull(argv[++i], NULL, 10);
        } else if (strcmp(a, "--dt") == 0) {
            p.dt = atof(argv[++i]);
        } else if (strcmp(a, "--sigma-a") == 0) {
            p.sigma_a = atof(argv[++i]);
        } else if (strcmp(a, "--sigma-r") == 0) {
            p.sigma_r = atof(argv[++i]);
        } else if (strcmp(a, "--sigma-p0") == 0) {
            p.sigma_p0 = atof(argv[++i]);
        } else if (strcmp(a, "--sigma-v0") == 0) {
            p.sigma_v0 = atof(argv[++i]);
        } else {
            return usage();
        }
    }
    if (out == NULL || tracks == 0 || steps == 0 || p.dt <= 0.0 || p.sigma_r <= 0.0) {
        return usage();
    }

    buf = (double *)malloc((size_t)steps * 2 * sizeof *buf);
    if (buf == NULL) {
        fprintf(stderr, "kfgen: out of memory\n");
        return 1;
    }

    kf_header_init(&hdr, KF_MAGIC_MEAS, 2, tracks, steps, &p);
    if (kf_writer_open(&w, out, &hdr) != 0) {
        free(buf);
        return 1;
    }
    t0 = plat_now_ns();
    for (t = 0; t < tracks; t++) {
        kf_gen_track(&p, t, (size_t)steps, NULL, buf);
        if (kf_writer_append(&w, buf, (size_t)steps * 2) != 0) {
            free(buf);
            return 1;
        }
    }
    if (kf_writer_close(&w) != 0) {
        free(buf);
        return 1;
    }
    t1 = plat_now_ns();
    free(buf);

    printf("kfgen: wrote %s: %llu tracks x %llu steps, seed %llu, %.1f MiB in %.2f s\n", out,
           (unsigned long long)tracks, (unsigned long long)steps, (unsigned long long)p.seed,
           (double)(KF_HEADER_BYTES + kf_header_data_bytes(&hdr)) / 1048576.0,
           (double)(t1 - t0) / 1e9);
    printf("data_hash=0x%016llx\n", (unsigned long long)w.hash);
    return 0;
}
