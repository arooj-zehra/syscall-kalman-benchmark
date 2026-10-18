/*
 * test_kalman - unit tests for the filter, the generator and the file format.
 * Known answers where they exist (published splitmix64 vectors, hand-computed
 * filter steps), invariants everywhere else. Exit 0 only if all pass.
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

/* Pinned bit patterns, printed by `python tools/kf_reference.py pins`
 * (seed 1, track 0, default parameters: measurement values 0 and 7). */
#define PIN_Z0_BITS 0xc066035fdce53ff7ull
#define PIN_Z7_BITS 0x4053ace8f5688e34ull

static int failures = 0;

#define CHECK(cond, what)                                   \
    do {                                                    \
        if (cond) {                                         \
            printf("  pass: %s\n", (what));                 \
        } else {                                            \
            printf("  FAIL: %s\n", (what));                 \
            failures++;                                     \
        }                                                   \
    } while (0)

static int near(double a, double b, double tol)
{
    return fabs(a - b) <= tol;
}

/* ---- generator ------------------------------------------------------------ */

static void test_rng(void)
{
    uint64_t sm = 0;
    kf_rng r;
    double mean = 0.0;
    double var = 0.0;
    double maxabs = 0.0;
    double umin = 1.0;
    double umax = 0.0;
    int i;
    const int N = 1000000;

    printf("random numbers\n");
    /* Published reference outputs of splitmix64 seeded with 0. */
    CHECK(kf_splitmix64(&sm) == 0xE220A8397B1DCDAFull, "splitmix64 output 1");
    CHECK(kf_splitmix64(&sm) == 0x6E789E6AA1B965F4ull, "splitmix64 output 2");
    CHECK(kf_splitmix64(&sm) == 0x06C45D188009454Full, "splitmix64 output 3");

    kf_rng_seed(&r, 12345, 7);
    for (i = 0; i < N; i++) {
        double u = kf_rng_uniform(&r);

        if (u < umin) {
            umin = u;
        }
        if (u > umax) {
            umax = u;
        }
        mean += u;
    }
    CHECK(umin >= 0.0 && umax < 1.0, "uniform stays in [0, 1)");
    CHECK(near(mean / N, 0.5, 0.002), "uniform mean is 0.5");

    kf_rng_seed(&r, 12345, 7);
    mean = 0.0;
    for (i = 0; i < N; i++) {
        double g = kf_rng_normal(&r);

        mean += g;
        var += g * g;
        if (fabs(g) > maxabs) {
            maxabs = fabs(g);
        }
    }
    mean /= N;
    var = var / N - mean * mean;
    CHECK(near(mean, 0.0, 0.005), "normal mean is 0");
    CHECK(near(var, 1.0, 0.01), "normal variance is 1");
    CHECK(maxabs <= 6.0, "normal is limited to +-6 sigma");
}

static void test_generator(void)
{
    kf_gen_params p;
    double a[2 * 300];
    double b[2 * 300];
    double c[2 * 300];
    double ta[4 * 300];
    int same_other_seed;
    int i;

    printf("generator\n");
    kf_gen_defaults(&p);
    p.seed = 9;

    kf_gen_track(&p, 3, 300, NULL, a);
    kf_gen_track(&p, 3, 300, ta, b);
    CHECK(memcmp(a, b, sizeof a) == 0, "same seed and track give identical measurements");
    CHECK(near(a[0], ta[0], 6.0 * p.sigma_r), "first measurement is near the first true position");

    kf_gen_track(&p, 4, 300, NULL, c);
    CHECK(memcmp(a, c, sizeof a) != 0, "a different track gives different data");

    p.seed = 10;
    kf_gen_track(&p, 3, 300, NULL, c);
    same_other_seed = 0;
    for (i = 0; i < 600; i++) {
        same_other_seed += a[i] == c[i];
    }
    CHECK(same_other_seed == 0, "a different seed changes every value");

    /* Pinned values (computed independently by tools/kf_reference.py). */
    p.seed = 1;
    kf_gen_track(&p, 0, 4, NULL, a);
    {
        uint64_t bits;

        memcpy(&bits, &a[0], 8);
        CHECK(bits == PIN_Z0_BITS, "first measurement of seed 1, track 0 matches the Python mirror");
        memcpy(&bits, &a[7], 8);
        CHECK(bits == PIN_Z7_BITS, "last of four steps matches the Python mirror");
    }
}

/* ---- filter -------------------------------------------------------------- */

static int is_positive_definite(const double *P)
{
    double L[16] = {0};
    int i;
    int j;
    int k;

    for (i = 0; i < 4; i++) {
        for (j = 0; j <= i; j++) {
            double sum = P[i * 4 + j];

            for (k = 0; k < j; k++) {
                sum -= L[i * 4 + k] * L[j * 4 + k];
            }
            if (i == j) {
                if (sum <= 0.0) {
                    return 0;
                }
                L[i * 4 + i] = sqrt(sum);
            } else {
                L[i * 4 + j] = sum / L[j * 4 + j];
            }
        }
    }
    return 1;
}

static void test_filter(void)
{
    kf_model m;
    kf_state s;
    double z[2];
    int k;

    printf("filter, hand-computed steps\n");

    /* dt = 0.5, sigma_a = 2: Q00 = 4*0.5^4/4, Q02 = 4*0.5^3/2, Q22 = 4*0.5^2. */
    kf_model_init(&m, 0.5, 2.0, 1.0, 100.0, 10.0);
    CHECK(near(m.Q[0], 0.0625, 1e-15) && near(m.Q[2], 0.25, 1e-15) && near(m.Q[10], 1.0, 1e-15),
          "process noise matrix Q");
    CHECK(near(m.F[2], 0.5, 0) && near(m.F[7], 0.5, 0) && m.F[0] == 1.0 && m.F[8] == 0.0, "motion matrix F");

    memset(&s, 0, sizeof s);
    s.x[2] = 1.0;
    s.x[3] = 2.0;
    kf_predict(&s, &m);
    CHECK(near(s.x[0], 0.5, 1e-15) && near(s.x[1], 1.0, 1e-15) && s.x[2] == 1.0 && s.x[3] == 2.0,
          "predict moves the position by velocity * dt");
    CHECK(near(s.P[0], 0.0625, 1e-15) && near(s.P[2], 0.25, 1e-15) && near(s.P[10], 1.0, 1e-15),
          "predict from zero covariance gives exactly Q");

    /* Position-only covariance 100, measurement variance 1, measurement (1, 0):
     * K = 100/101, x = 100/101, P00 = 100/101, velocities untouched. */
    memset(&s, 0, sizeof s);
    s.P[0] = 100.0;
    s.P[5] = 100.0;
    z[0] = 1.0;
    z[1] = 0.0;
    {
        double nis;

        kf_update(&s, &m, z, &nis);
        CHECK(near(s.x[0], 100.0 / 101.0, 1e-14) && near(s.x[1], 0.0, 1e-14), "update: position estimate");
        CHECK(near(s.P[0], 100.0 / 101.0, 1e-13) && near(s.P[5], 100.0 / 101.0, 1e-13),
              "update: position variance 100*1/(100+1)");
        CHECK(near(nis, 1.0 / 101.0, 1e-15), "update: NIS = y^2 / S");
    }

    printf("filter, behaviour\n");

    /* Noise-free straight line with a tiny process noise: the filter must
     * lock on to the exact position and velocity. */
    {
        double meas[2 * 300];
        double est[4 * 300];

        kf_model_init(&m, 0.1, 1e-6, 1.0, 1000.0, 10.0);
        for (k = 0; k < 300; k++) {
            meas[2 * k] = 5.0 + 3.0 * 0.1 * k;
            meas[2 * k + 1] = -7.0 + 1.5 * 0.1 * k;
        }
        kf_run_track(&m, meas, 300, est);
        CHECK(near(est[4 * 299], meas[2 * 299], 1e-2), "noise-free line: position locked");
        CHECK(near(est[4 * 299 + 2], 3.0, 1e-2) && near(est[4 * 299 + 3], 1.5, 1e-2),
              "noise-free line: velocity recovered");
    }

    /* Long run on noisy data: P must stay symmetric and positive definite. */
    {
        kf_gen_params p;
        static double meas[2 * 100000];
        kf_state st;
        int sym = 1;
        int i;
        int j;

        kf_gen_defaults(&p);
        kf_gen_track(&p, 0, 100000, NULL, meas);
        kf_model_init(&m, p.dt, p.sigma_a, p.sigma_r, p.sigma_p0, p.sigma_v0);
        kf_state_init(&st, &m);
        for (k = 0; k < 100000; k++) {
            if (k > 0) {
                kf_predict(&st, &m);
            }
            kf_update(&st, &m, meas + 2 * k, NULL);
        }
        for (i = 0; i < 4; i++) {
            for (j = 0; j < 4; j++) {
                if (st.P[i * 4 + j] != st.P[j * 4 + i]) {
                    sym = 0;
                }
            }
        }
        CHECK(sym, "covariance is exactly symmetric after 100,000 steps");
        CHECK(is_positive_definite(st.P), "covariance is positive definite after 100,000 steps");
    }
}

/* ---- file format --------------------------------------------------------- */

static void test_files(void)
{
    const char *path = "sk_test_kf.bin";
    kf_gen_params p;
    kf_file_header h;
    kf_writer w;
    kf_reader r;
    double buf[2 * 50];
    double all[3 * 2 * 50];
    uint64_t t;
    int mode;
    uint64_t h12[3];
    const double one_two_three[3] = {1.0, 2.0, 3.0};

    printf("file format\n");
    h12[0] = kf_hash_update(KF_HASH_INIT, one_two_three, sizeof one_two_three);
    CHECK(h12[0] == 0x899512186c0f2fb7ull, "hash of three known doubles (value from the Python mirror)");

    kf_gen_defaults(&p);
    p.seed = 77;
    kf_header_init(&h, KF_MAGIC_MEAS, 2, 3, 50, &p);
    CHECK(kf_writer_open(&w, path, &h) == 0, "create a file");
    for (t = 0; t < 3; t++) {
        kf_gen_track(&p, t, 50, NULL, buf);
        memcpy(all + t * 100, buf, sizeof buf);
        CHECK(kf_writer_append(&w, buf, 100) == 0, "append a track");
    }
    CHECK(kf_writer_close(&w) == 0, "close and patch the header hash");
    CHECK(w.hash == kf_hash_update(KF_HASH_INIT, all, sizeof all), "writer hash equals hash of the data");

    for (mode = 0; mode <= 1; mode++) {
        char what[96];

        snprintf(what, sizeof what, "open with %s", mode ? "mmap" : "plat_read");
        CHECK(kf_reader_open(&r, path, KF_MAGIC_MEAS, mode) == 0, what);
        CHECK(r.hdr.f.n_tracks == 3 && r.hdr.f.n_steps == 50 && r.hdr.f.seed == 77, "header fields survive");
        CHECK(r.hdr.f.dt == p.dt && r.hdr.f.sigma_r == p.sigma_r, "model parameters survive");
        CHECK(r.hdr.f.data_hash == w.hash, "header holds the data hash");
        CHECK(kf_reader_hash(&r) == r.hdr.f.data_hash, "hash of the data read equals the header hash");
        CHECK(memcmp(r.data, all, sizeof all) == 0, "data read equals data written");
        kf_reader_close(&r);
    }

    printf("  (the next line logs an expected ERROR: wrong magic)\n");
    CHECK(kf_reader_open(&r, path, KF_MAGIC_EST, 1) == -1, "opening a measurement file as an estimate file is refused");
    CHECK(plat_unlink(path) == 0, "remove the test file");
}

int main(void)
{
    log_init(NULL);
    test_rng();
    test_generator();
    test_filter();
    test_files();
    if (failures == 0) {
        printf("test_kalman PASSED\n");
    } else {
        printf("test_kalman FAILED (%d check(s))\n", failures);
    }
    return failures == 0 ? 0 : 1;
}
