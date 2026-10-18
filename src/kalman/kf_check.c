/*
 * kf_check - does the filter behave like a correct Kalman filter?
 *
 * Runs entirely in memory on freshly generated data and tests statistics that
 * hold only if filter and data agree:
 *
 *   NEES  (x - x_true)' P^-1 (x - x_true)   mean 4  (4 states)   - the error is as
 *                                           large as the filter thinks it is
 *   NIS   y' S^-1 y                         mean 2  (2 measurements) - innovations
 *                                           have the predicted size
 *   noise std of (z - truth)                equals sigma_r (generator check)
 *   RMSE  filter position error             equals sqrt(P00 + P11) at steady state
 *   gain  raw error / filter error          well above 1
 *
 * Exit status 0 only if every check passes. Prints every measured value.
 */

#include "datagen.h"
#include "kalman.h"
#include "log.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#define TRACKS 8
#define STEPS  4000
#define BURN   200

static int failures = 0;

static void check_range(const char *what, double v, double lo, double hi)
{
    int ok = v >= lo && v <= hi;

    printf("  %-44s %9.4f   expected %.3f .. %.3f   %s\n", what, v, lo, hi, ok ? "ok" : "FAIL");
    if (!ok) {
        failures++;
    }
}

/* Solve A x = b for a symmetric positive definite 4x4 matrix (Gaussian elimination). */
static void solve4(const double *A, const double *b, double *x)
{
    double M[4][5];
    int i;
    int j;
    int k;

    for (i = 0; i < 4; i++) {
        for (j = 0; j < 4; j++) {
            M[i][j] = A[i * 4 + j];
        }
        M[i][4] = b[i];
    }
    for (i = 0; i < 4; i++) {
        double piv = M[i][i];

        for (j = i; j < 5; j++) {
            M[i][j] /= piv;
        }
        for (k = 0; k < 4; k++) {
            if (k != i) {
                double f = M[k][i];

                for (j = i; j < 5; j++) {
                    M[k][j] -= f * M[i][j];
                }
            }
        }
    }
    for (i = 0; i < 4; i++) {
        x[i] = M[i][4];
    }
}

int main(void)
{
    kf_gen_params p;
    kf_model model;
    kf_state s;
    kf_state ss;
    static double truth[STEPS * 4];
    static double meas[STEPS * 2];
    double nees_sum = 0.0;
    double nis_sum = 0.0;
    double n = 0.0;
    double noise_sum = 0.0;
    double noise_sq = 0.0;
    double sq_meas = 0.0;
    double sq_filt = 0.0;
    double theory;
    uint64_t t;
    int k;

    log_init(NULL);
    kf_gen_defaults(&p);
    p.seed = 20241;
    kf_model_init(&model, p.dt, p.sigma_a, p.sigma_r, p.sigma_p0, p.sigma_v0);

    for (t = 0; t < TRACKS; t++) {
        kf_gen_track(&p, t, STEPS, truth, meas);
        kf_state_init(&s, &model);
        for (k = 0; k < STEPS; k++) {
            double nis;

            if (k > 0) {
                kf_predict(&s, &model);
            }
            kf_update(&s, &model, meas + 2 * k, &nis);
            if (k >= BURN) {
                double e[4];
                double w[4];
                double nees = 0.0;
                double dx = meas[2 * k] - truth[4 * k];
                double dy = meas[2 * k + 1] - truth[4 * k + 1];
                int i;

                for (i = 0; i < 4; i++) {
                    e[i] = s.x[i] - truth[4 * k + i];
                }
                solve4(s.P, e, w);
                for (i = 0; i < 4; i++) {
                    nees += e[i] * w[i];
                }
                nees_sum += nees;
                nis_sum += nis;
                noise_sum += dx + dy;
                noise_sq += dx * dx + dy * dy;
                sq_meas += dx * dx + dy * dy;
                sq_filt += e[0] * e[0] + e[1] * e[1];
                n += 1.0;
            }
        }
    }

    /* Predicted steady-state position error from P alone. */
    kf_state_init(&ss, &model);
    for (k = 0; k < 5000; k++) {
        double z0[2] = {0.0, 0.0};

        if (k > 0) {
            kf_predict(&ss, &model);
        }
        kf_update(&ss, &model, z0, NULL);
    }
    theory = sqrt(ss.P[0] + ss.P[5]);

    printf("kf_check: %d tracks x %d steps, first %d steps of each skipped\n", TRACKS, STEPS, BURN);
    check_range("mean NEES (4 states)", nees_sum / n, 3.7, 4.3);
    check_range("mean NIS (2 measurements)", nis_sum / n, 1.9, 2.1);
    check_range("measurement noise mean (m)", noise_sum / (2.0 * n), -0.08, 0.08);
    check_range("measurement noise std / sigma_r", sqrt(noise_sq / (2.0 * n)) / p.sigma_r, 0.97, 1.03);
    check_range("filter position RMSE / predicted", sqrt(sq_filt / n) / theory, 0.95, 1.05);
    check_range("noise reduction (raw / filter)", sqrt(sq_meas / n) / sqrt(sq_filt / n), 2.0, 100.0);

    if (failures == 0) {
        printf("kf_check PASSED\n");
    } else {
        printf("kf_check FAILED (%d check(s))\n", failures);
    }
    return failures == 0 ? 0 : 1;
}
