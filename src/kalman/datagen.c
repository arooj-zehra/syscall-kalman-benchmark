#include "datagen.h"

void kf_gen_defaults(kf_gen_params *p)
{
    p->dt = 0.1;
    p->sigma_a = 0.5;
    p->sigma_r = 5.0;
    p->sigma_p0 = 1000.0;
    p->sigma_v0 = 10.0;
    p->seed = 1;
}

uint64_t kf_splitmix64(uint64_t *state)
{
    uint64_t z;

    *state += 0x9E3779B97F4A7C15ull;
    z = *state;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

static uint64_t rotl64(uint64_t x, int k)
{
    return (x << k) | (x >> (64 - k));
}

void kf_rng_seed(kf_rng *r, uint64_t seed, uint64_t track)
{
    /* Mix the seed once, then fold in the track number multiplicatively, so that
     * neighbouring tracks do not get overlapping splitmix sequences. */
    uint64_t sm = seed;
    uint64_t first = kf_splitmix64(&sm);
    int i;

    sm = first ^ (track * 0xD1B54A32D192ED03ull);
    for (i = 0; i < 4; i++) {
        r->s[i] = kf_splitmix64(&sm);
    }
}

uint64_t kf_rng_next(kf_rng *r)
{
    uint64_t result = rotl64(r->s[1] * 5u, 7) * 9u;
    uint64_t t = r->s[1] << 17;

    r->s[2] ^= r->s[0];
    r->s[3] ^= r->s[1];
    r->s[1] ^= r->s[2];
    r->s[0] ^= r->s[3];
    r->s[2] ^= t;
    r->s[3] = rotl64(r->s[3], 45);
    return result;
}

double kf_rng_uniform(kf_rng *r)
{
    return (double)(kf_rng_next(r) >> 11) * (1.0 / 9007199254740992.0);
}

double kf_rng_normal(kf_rng *r)
{
    double sum = 0.0;
    int i;

    for (i = 0; i < 12; i++) {
        sum += kf_rng_uniform(r);
    }
    return sum - 6.0;
}

void kf_gen_track(const kf_gen_params *p, uint64_t track, size_t n_steps,
                  double *truth, double *meas)
{
    kf_rng rng;
    double dt = p->dt;
    double half_dt2 = 0.5 * dt * dt;
    double px;
    double py;
    double vx;
    double vy;
    size_t k;

    kf_rng_seed(&rng, p->seed, track);

    /* Fixed draw order: px, py, vx, vy. */
    px = p->sigma_p0 * kf_rng_normal(&rng);
    py = p->sigma_p0 * kf_rng_normal(&rng);
    vx = p->sigma_v0 * kf_rng_normal(&rng);
    vy = p->sigma_v0 * kf_rng_normal(&rng);

    for (k = 0; k < n_steps; k++) {
        if (k > 0) {
            double ax = p->sigma_a * kf_rng_normal(&rng);
            double ay = p->sigma_a * kf_rng_normal(&rng);

            px = px + vx * dt + ax * half_dt2;
            py = py + vy * dt + ay * half_dt2;
            vx = vx + ax * dt;
            vy = vy + ay * dt;
        }
        /* Measurement noise: x first, then y. */
        meas[2 * k + 0] = px + p->sigma_r * kf_rng_normal(&rng);
        meas[2 * k + 1] = py + p->sigma_r * kf_rng_normal(&rng);
        if (truth != NULL) {
            truth[4 * k + 0] = px;
            truth[4 * k + 1] = py;
            truth[4 * k + 2] = vx;
            truth[4 * k + 3] = vy;
        }
    }
}
