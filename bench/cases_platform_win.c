/* Windows has no OS-specific extra cases yet (fork() does not exist there). */

#include "bench.h"

void bench_cases_platform(bench_list *list)
{
    (void)list;
}
