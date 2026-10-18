/*
 * datagen.h - deterministic synthetic data for the Kalman filter.
 *
 * Every track has its own random stream derived from (seed, track number), so
 * any track can be regenerated on its own, in any order, by any process. The
 * generator uses only + - * on doubles (no libm calls): the same seed gives the
 * same bits on Linux and Windows.
 *
 *   random bits   xoshiro256** (seeded through splitmix64)
 *   uniform       top 53 bits of a draw, times 2^-53, in [0, 1)
 *   normal        Irwin-Hall: sum of 12 uniforms minus 6 (mean 0, variance 1,
 *                 limited to +-6 sigma; no log/cos, so no libm differences)
 *
 * Truth per track (the same model the filter assumes):
 *   start     position ~ N(0, sigma_p0^2), velocity ~ N(0, sigma_v0^2), per axis
 *   each step acceleration a ~ N(0, sigma_a^2) per axis:
 *               p += v dt + a dt^2/2 ;  v += a dt
 *   measure   z = p + N(0, sigma_r^2) per axis
 * Draw order per step is fixed (documented in docs/KALMAN_DESIGN.md) because
 * tools/kf_reference.py mirrors it to cross-check this code.
 */
#ifndef DATAGEN_H
#define DATAGEN_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    double   dt;
    double   sigma_a;
    double   sigma_r;
    double   sigma_p0;
    double   sigma_v0;
    uint64_t seed;
} kf_gen_params;

/* dt 0.1 s, sigma_a 0.5 m/s^2, sigma_r 5 m, sigma_p0 1000 m, sigma_v0 10 m/s. */
void kf_gen_defaults(kf_gen_params *p);

typedef struct {
    uint64_t s[4];
} kf_rng;

uint64_t kf_splitmix64(uint64_t *state);
void     kf_rng_seed(kf_rng *r, uint64_t seed, uint64_t track);
uint64_t kf_rng_next(kf_rng *r);
double   kf_rng_uniform(kf_rng *r);
double   kf_rng_normal(kf_rng *r);

/* Generate one track. meas receives n_steps pairs; truth (may be NULL) receives
 * n_steps quadruples [px, py, vx, vy]. */
void kf_gen_track(const kf_gen_params *p, uint64_t track, size_t n_steps,
                  double *truth, double *meas);

#ifdef __cplusplus
}
#endif

#endif /* DATAGEN_H */
