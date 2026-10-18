/*
 * kalman.h - the Kalman filter itself. Pure computation: no operating-system
 * code, no I/O, no allocation. The serial program and (in Phase 6) every worker
 * process call exactly these functions, so serial and parallel results can be
 * compared bit for bit.
 *
 * Model: a target moving in a plane with (nearly) constant velocity.
 *
 *   state        x = [px, py, vx, vy]            position (m) and velocity (m/s)
 *   motion       x(k) = F x(k-1) + w,   w = white-noise acceleration, std sigma_a
 *   measurement  z(k) = H x(k) + v,     z = [px, py], v ~ N(0, sigma_r^2 I)
 *
 *   F = | 1 0 dt 0 |      H = | 1 0 0 0 |
 *       | 0 1 0 dt |          | 0 1 0 0 |
 *       | 0 0 1  0 |
 *       | 0 0 0  1 |
 *
 *   Q (per axis, on [position, velocity]) = sigma_a^2 * | dt^4/4  dt^3/2 |
 *                                                      | dt^3/2  dt^2   |
 *
 * Each step is "predict" (x = F x, P = F P F' + Q) then "update" with the
 * measurement. The covariance update uses the Joseph form, which keeps P
 * symmetric and positive definite over millions of steps.
 *
 * Matrices are 4x4 stored row-major in double[16].
 */
#ifndef KALMAN_H
#define KALMAN_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    double dt;
    double q;        /* sigma_a squared */
    double r;        /* sigma_r squared */
    double p0;       /* prior position variance (sigma_p0 squared) */
    double v0;       /* prior velocity variance (sigma_v0 squared) */
    double F[16];
    double Q[16];
} kf_model;

typedef struct {
    double x[4];
    double P[16];
} kf_state;

void kf_model_init(kf_model *m, double dt, double sigma_a, double sigma_r,
                   double sigma_p0, double sigma_v0);

/* Prior before the first measurement: x = 0, P = diag(p0, p0, v0, v0). */
void kf_state_init(kf_state *s, const kf_model *m);

void kf_predict(kf_state *s, const kf_model *m);

/* Fold in measurement z[2]. If nis is not NULL it receives the normalised
 * innovation squared y' S^-1 y (mean 2 for a consistent filter). */
void kf_update(kf_state *s, const kf_model *m, const double z[2], double *nis);

/* Run the filter over n measurements (meas holds n pairs) and store the
 * posterior state after each one in est (n quadruples). The first measurement
 * is applied to the prior without a predict step. */
void kf_run_track(const kf_model *m, const double *meas, size_t n, double *est);

#ifdef __cplusplus
}
#endif

#endif /* KALMAN_H */
