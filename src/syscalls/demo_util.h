/*
 * demo_util.h - shared helpers for the small demo programs in this folder.
 *
 * Every demo prints one line per check (pass or FAIL), then a final
 * "DEMO <name> PASSED" or "DEMO <name> FAILED", and exits 0 only on success, so
 * ctest and the benchmark scripts can rely on the exit code.
 */
#ifndef DEMO_UTIL_H
#define DEMO_UTIL_H

#include "log.h"

#include <stdio.h>
#include <string.h>

/* Deterministic byte pattern, so a reader can verify data it did not write. */
#define DEMO_PATTERN(i) ((unsigned char)(((i) * 7u + 3u) & 0xffu))

static inline void demo_begin(int argc, char **argv, const char *name)
{
    char default_log[128];

    snprintf(default_log, sizeof default_log, "demo_%s.log", name);
    log_init(argc > 1 ? argv[1] : default_log);
    LOG_INFO("==== demo_%s ====", name);
}

static inline int demo_finish(const char *name, int failures)
{
    if (failures == 0) {
        LOG_INFO("DEMO %s PASSED", name);
    } else {
        LOG_ERROR("DEMO %s FAILED (%d check(s) failed)", name, failures);
    }
    log_close();
    return failures == 0 ? 0 : 1;
}

#define DEMO_CHECK(failures, cond, what)           \
    do {                                           \
        if (cond) {                                \
            LOG_INFO("  pass: %s", (what));        \
        } else {                                   \
            LOG_ERROR("  FAIL: %s", (what));       \
            (failures)++;                          \
        }                                          \
    } while (0)

/* Several demos start child_helper, which sits next to the demo executable.
 * CMake defines HELPER_NAME ("child_helper" or "child_helper.exe"). */
#ifdef HELPER_NAME
static inline void demo_helper_path(const char *argv0, char *out, size_t n)
{
    const char *fwd = strrchr(argv0, '/');
    const char *back = strrchr(argv0, '\\');
    const char *sep;

    if (fwd != NULL && back != NULL) {
        sep = fwd > back ? fwd : back;
    } else {
        sep = fwd != NULL ? fwd : back;
    }
    if (sep != NULL) {
        snprintf(out, n, "%.*s%s", (int)(sep - argv0) + 1, argv0, HELPER_NAME);
    } else {
        snprintf(out, n, "./%s", HELPER_NAME);
    }
}
#endif

#endif /* DEMO_UTIL_H */
