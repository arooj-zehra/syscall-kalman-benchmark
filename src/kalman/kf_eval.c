/*
 * kf_eval - how good are the estimates in a file?
 *
 *   kf_eval --meas data/meas.bin --est data/est_serial.bin [--burn-in 100] [--check]
 *
 * The true trajectories are not stored: they are regenerated from the seed in
 * the header (the generator is deterministic). The program
 *   1. checks that the regenerated measurements equal the ones in the file,
 *   2. reports the position error of the raw measurements and of the filter,
 *   3. compares the filter's error with its own predicted steady-state error,
 *      which comes from the covariance P and needs no data at all.
 * With --check it exits non-zero if (1) fails or (3) is off by more than 10 %.
 */

#include "datagen.h"
#include "kalman.h"
#include "kfio.h"
#include "log.h"
#include "platform.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int usage(void)
{
    fprintf(stderr, "usage: kf_eval --meas FILE --est FILE [--burn-in N] [--check]\n");
    return 2;
}

int main(int argc, char **argv)
{
    const char *meas_path = NULL;
    const char *est_path = NULL;
    size_t burn = 100;
    int check = 0;
    kf_reader rm;
    kf_reader re;
    kf_gen_params p;
    kf_model model;
    kf_state ss;
    double *truth;
    double *meas;
    uint64_t n_tracks;
    uint64_t n_steps;
    uint64_t t;
    size_t k;
    double sq_meas = 0.0;
    double sq_pos = 0.0;
    double sq_vel = 0.0;
    double n_meas = 0.0;
    double n_filt = 0.0;
    double theory_pos;
    double theory_vel;
    double rmse_meas;
    double rmse_pos;
    double rmse_vel;
    int regen_ok = 1;
    int i;
    int rc = 0;

    log_init(NULL);
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--check") == 0) {
            check = 1;
        } else if (i + 1 >= argc) {
            return usage();
        } else if (strcmp(argv[i], "--meas") == 0) {
            meas_path = argv[++i];
        } else if (strcmp(argv[i], "--est") == 0) {
            est_path = argv[++i];
        } else if (strcmp(argv[i], "--burn-in") == 0) {
            burn = (size_t)strtoull(argv[++i], NULL, 10);
        } else {
            return usage();
        }
    }
    if (meas_path == NULL || est_path == NULL) {
        return usage();
    }
    if (kf_reader_open(&rm, meas_path, KF_MAGIC_MEAS, 1) != 0) {
        return 1;
    }
    if (kf_reader_open(&re, est_path, KF_MAGIC_EST, 1) != 0) {
        kf_reader_close(&rm);
        return 1;
    }
    n_tracks = rm.hdr.f.n_tracks;
    n_steps = rm.hdr.f.n_steps;
    if (re.hdr.f.n_tracks != n_tracks || re.hdr.f.n_steps != n_steps || re.hdr.f.seed != rm.hdr.f.seed) {
        LOG_ERROR("the two files do not describe the same data set");
        kf_reader_close(&rm);
        kf_reader_close(&re);
        return 1;
    }
    if (burn >= n_steps) {
        burn = (size_t)n_steps / 2;
    }
    kf_header_params(&rm.hdr, &p);
    kf_model_init(&model, p.dt, p.sigma_a, p.sigma_r, p.sigma_p0, p.sigma_v0);

    truth = (double *)malloc((size_t)n_steps * 4 * sizeof *truth);
    meas = (double *)malloc((size_t)n_steps * 2 * sizeof *meas);
    if (truth == NULL || meas == NULL) {
        LOG_ERROR("out of memory");
        return 1;
    }

    for (t = 0; t < n_tracks; t++) {
        const double *zf = rm.data + t * n_steps * 2;
        const double *ef = re.data + t * n_steps * 4;

        kf_gen_track(&p, t, (size_t)n_steps, truth, meas);
        if (memcmp(meas, zf, (size_t)n_steps * 2 * sizeof *meas) != 0) {
            regen_ok = 0;
        }
        for (k = 0; k < n_steps; k++) {
            double mx = zf[2 * k] - truth[4 * k];
            double my = zf[2 * k + 1] - truth[4 * k + 1];

            if (k >= burn) {
                double ex = ef[4 * k] - truth[4 * k];
                double ey = ef[4 * k + 1] - truth[4 * k + 1];
                double evx = ef[4 * k + 2] - truth[4 * k + 2];
                double evy = ef[4 * k + 3] - truth[4 * k + 3];

                sq_meas += mx * mx + my * my;
                n_meas += 1.0;
                sq_pos += ex * ex + ey * ey;
                sq_vel += evx * evx + evy * evy;
                n_filt += 1.0;
            }
        }
    }

    /* Predicted steady state: P does not depend on the data, so iterate it with z = 0. */
    kf_state_init(&ss, &model);
    for (k = 0; k < 5000; k++) {
        double z0[2] = {0.0, 0.0};

        if (k > 0) {
            kf_predict(&ss, &model);
        }
        kf_update(&ss, &model, z0, NULL);
    }
    theory_pos = sqrt(ss.P[0] + ss.P[5]);
    theory_vel = sqrt(ss.P[10] + ss.P[15]);

    rmse_meas = sqrt(sq_meas / n_meas);
    rmse_pos = sqrt(sq_pos / n_filt);
    rmse_vel = sqrt(sq_vel / n_filt);

    printf("kf_eval: %llu tracks x %llu steps (first %llu steps of each track skipped)\n",
           (unsigned long long)n_tracks, (unsigned long long)n_steps, (unsigned long long)burn);
    printf("  regenerated measurements equal the file : %s\n", regen_ok ? "yes" : "NO");
    printf("  position error (2-D RMS)  raw measurement : %8.4f m\n", rmse_meas);
    printf("                            filter          : %8.4f m   (predicted %.4f m, ratio %.3f)\n",
           rmse_pos, theory_pos, rmse_pos / theory_pos);
    printf("  velocity error (2-D RMS)  filter          : %8.4f m/s (predicted %.4f m/s, ratio %.3f)\n",
           rmse_vel, theory_vel, rmse_vel / theory_vel);
    printf("  noise reduction (raw / filter)            : %8.2f x\n", rmse_meas / rmse_pos);

    if (check) {
        if (!regen_ok) {
            printf("CHECK FAILED: regenerated measurements differ from the file\n");
            rc = 1;
        }
        if (fabs(rmse_pos / theory_pos - 1.0) > 0.10 || fabs(rmse_vel / theory_vel - 1.0) > 0.10) {
            printf("CHECK FAILED: filter error is more than 10%% away from its predicted value\n");
            rc = 1;
        }
        if (rc == 0) {
            printf("CHECK PASSED\n");
        }
    }

    free(truth);
    free(meas);
    kf_reader_close(&rm);
    kf_reader_close(&re);
    return rc;
}
