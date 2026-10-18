/* Unit test for bench_compute_stats: known inputs, known answers. */

#include "bench.h"

#include <math.h>
#include <stdio.h>

static int failures = 0;

static void expect(const char *what, double got, double want)
{
    if (fabs(got - want) > 1e-9) {
        printf("FAIL %s: got %.12g, want %.12g\n", what, got, want);
        failures++;
    } else {
        printf("pass %s = %.12g\n", what, got);
    }
}

int main(void)
{
    /* 1..10: mean 5.5, median 5.5, sample stddev sqrt(82.5/9), p95 = 9.55 */
    double a[10] = {10, 3, 7, 1, 9, 2, 8, 4, 6, 5};
    double b[1] = {42};
    double c[4] = {5, 5, 5, 5};
    bench_stats s;

    if (bench_compute_stats(a, 10, &s) != 0) {
        return 2;
    }
    expect("n", s.n, 10);
    expect("mean", s.mean, 5.5);
    expect("median", s.median, 5.5);
    expect("stddev", s.stddev, sqrt(82.5 / 9.0));
    expect("p95", s.p95, 9.55);
    expect("min", s.min, 1);
    expect("max", s.max, 10);

    if (bench_compute_stats(b, 1, &s) != 0) {
        return 2;
    }
    expect("single mean", s.mean, 42);
    expect("single p95", s.p95, 42);
    expect("single stddev", s.stddev, 0);

    if (bench_compute_stats(c, 4, &s) != 0) {
        return 2;
    }
    expect("constant stddev", s.stddev, 0);
    expect("constant median", s.median, 5);

    if (bench_compute_stats(c, 0, &s) != -1) {
        printf("FAIL empty input must be rejected\n");
        failures++;
    }

    printf(failures == 0 ? "stats test PASSED\n" : "stats test FAILED\n");
    return failures == 0 ? 0 : 1;
}
