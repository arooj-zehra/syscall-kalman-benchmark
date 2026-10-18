#include "kalman.h"

#include <string.h>

void kf_model_init(kf_model *m, double dt, double sigma_a, double sigma_r,
                   double sigma_p0, double sigma_v0)
{
    double q = sigma_a * sigma_a;
    double dt2 = dt * dt;
    double dt3 = dt2 * dt;
    double dt4 = dt2 * dt2;
    int i;

    memset(m, 0, sizeof *m);
    m->dt = dt;
    m->q = q;
    m->r = sigma_r * sigma_r;
    m->p0 = sigma_p0 * sigma_p0;
    m->v0 = sigma_v0 * sigma_v0;

    for (i = 0; i < 4; i++) {
        m->F[i * 4 + i] = 1.0;
    }
    m->F[0 * 4 + 2] = dt;
    m->F[1 * 4 + 3] = dt;

    m->Q[0 * 4 + 0] = q * dt4 / 4.0;
    m->Q[0 * 4 + 2] = q * dt3 / 2.0;
    m->Q[2 * 4 + 0] = q * dt3 / 2.0;
    m->Q[2 * 4 + 2] = q * dt2;
    m->Q[1 * 4 + 1] = q * dt4 / 4.0;
    m->Q[1 * 4 + 3] = q * dt3 / 2.0;
    m->Q[3 * 4 + 1] = q * dt3 / 2.0;
    m->Q[3 * 4 + 3] = q * dt2;
}

void kf_state_init(kf_state *s, const kf_model *m)
{
    memset(s, 0, sizeof *s);
    s->P[0 * 4 + 0] = m->p0;
    s->P[1 * 4 + 1] = m->p0;
    s->P[2 * 4 + 2] = m->v0;
    s->P[3 * 4 + 3] = m->v0;
}

/* C = A * B */
static void mul4(const double *A, const double *B, double *C)
{
    int i;
    int j;
    int k;

    for (i = 0; i < 4; i++) {
        for (j = 0; j < 4; j++) {
            double sum = 0.0;
            for (k = 0; k < 4; k++) {
                sum += A[i * 4 + k] * B[k * 4 + j];
            }
            C[i * 4 + j] = sum;
        }
    }
}

/* C = A * B' */
static void mul4_bt(const double *A, const double *B, double *C)
{
    int i;
    int j;
    int k;

    for (i = 0; i < 4; i++) {
        for (j = 0; j < 4; j++) {
            double sum = 0.0;
            for (k = 0; k < 4; k++) {
                sum += A[i * 4 + k] * B[j * 4 + k];
            }
            C[i * 4 + j] = sum;
        }
    }
}

void kf_predict(kf_state *s, const kf_model *m)
{
    double x[4];
    double T[16];
    double P[16];
    int i;
    int k;

    for (i = 0; i < 4; i++) {
        double sum = 0.0;
        for (k = 0; k < 4; k++) {
            sum += m->F[i * 4 + k] * s->x[k];
        }
        x[i] = sum;
    }
    mul4(m->F, s->P, T);
    mul4_bt(T, m->F, P);
    for (i = 0; i < 16; i++) {
        s->P[i] = P[i] + m->Q[i];
    }
    memcpy(s->x, x, sizeof x);
}

void kf_update(kf_state *s, const kf_model *m, const double z[2], double *nis)
{
    const double *P = s->P;
    double S00 = P[0 * 4 + 0] + m->r;
    double S01 = P[0 * 4 + 1];
    double S10 = P[1 * 4 + 0];
    double S11 = P[1 * 4 + 1] + m->r;
    double det = S00 * S11 - S01 * S10;
    double i00 = S11 / det;
    double i01 = -S01 / det;
    double i10 = -S10 / det;
    double i11 = S00 / det;
    double y0 = z[0] - s->x[0];
    double y1 = z[1] - s->x[1];
    double K[8];   /* 4x2 gain, row-major */
    double A[16];  /* I - K H */
    double T[16];
    double Pn[16];
    int i;
    int j;

    for (i = 0; i < 4; i++) {
        K[i * 2 + 0] = P[i * 4 + 0] * i00 + P[i * 4 + 1] * i10;
        K[i * 2 + 1] = P[i * 4 + 0] * i01 + P[i * 4 + 1] * i11;
    }
    for (i = 0; i < 4; i++) {
        s->x[i] += K[i * 2 + 0] * y0 + K[i * 2 + 1] * y1;
    }
    if (nis != NULL) {
        *nis = y0 * (i00 * y0 + i01 * y1) + y1 * (i10 * y0 + i11 * y1);
    }

    /* Joseph form: P = A P A' + K R K', with R = r I. */
    for (i = 0; i < 4; i++) {
        for (j = 0; j < 4; j++) {
            A[i * 4 + j] = (i == j ? 1.0 : 0.0) - (j < 2 ? K[i * 2 + j] : 0.0);
        }
    }
    mul4(A, P, T);
    mul4_bt(T, A, Pn);
    for (i = 0; i < 4; i++) {
        for (j = 0; j < 4; j++) {
            Pn[i * 4 + j] += m->r * (K[i * 2 + 0] * K[j * 2 + 0] + K[i * 2 + 1] * K[j * 2 + 1]);
        }
    }
    /* Remove the last rounding asymmetry. */
    for (i = 0; i < 4; i++) {
        for (j = i; j < 4; j++) {
            double v = 0.5 * (Pn[i * 4 + j] + Pn[j * 4 + i]);
            s->P[i * 4 + j] = v;
            s->P[j * 4 + i] = v;
        }
    }
}

void kf_run_track(const kf_model *m, const double *meas, size_t n, double *est)
{
    kf_state s;
    size_t k;

    kf_state_init(&s, m);
    for (k = 0; k < n; k++) {
        if (k > 0) {
            kf_predict(&s, m);
        }
        kf_update(&s, m, meas + 2 * k, NULL);
        memcpy(est + 4 * k, s.x, sizeof s.x);
    }
}
