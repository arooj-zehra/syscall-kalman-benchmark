/* Statistics for benchmark samples. Kept free of OS code so it can be unit-tested. */

#include "bench.h"

#include <math.h>
#include <stdlib.h>

static int cmp_double(const void *a, const void *b)
{
    double x = *(const double *)a;
    double y = *(const double *)b;

    return (x > y) - (x < y);
}

/* Percentile q in [0,1] of a sorted array, linear interpolation. */
static double percentile(const double *sorted, int n, double q)
{
    double pos;
    int lo;
    double frac;

    if (n == 1) {
        return sorted[0];
    }
    pos = q * (double)(n - 1);
    lo = (int)pos;
    frac = pos - (double)lo;
    if (lo + 1 >= n) {
        return sorted[n - 1];
    }
    return sorted[lo] + frac * (sorted[lo + 1] - sorted[lo]);
}

int bench_compute_stats(double *v, int n, bench_stats *out)
{
    double sum = 0.0;
    double sq = 0.0;
    int i;

    if (n < 1) {
        return -1;
    }
    qsort(v, (size_t)n, sizeof v[0], cmp_double);

    for (i = 0; i < n; i++) {
        sum += v[i];
    }
    out->n = n;
    out->mean = sum / (double)n;
    for (i = 0; i < n; i++) {
        double d = v[i] - out->mean;
        sq += d * d;
    }
    out->stddev = n > 1 ? sqrt(sq / (double)(n - 1)) : 0.0;
    out->min = v[0];
    out->max = v[n - 1];
    out->median = percentile(v, n, 0.5);
    out->p95 = percentile(v, n, 0.95);
    return 0;
}

int bench_add(bench_list *list, const bench_case *c)
{
    if (list->count >= BENCH_MAX_CASES) {
        return -1;
    }
    list->items[list->count++] = *c;
    return 0;
}
