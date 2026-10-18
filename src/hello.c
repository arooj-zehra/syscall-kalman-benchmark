/*
 * hello.c - Phase 1 smoke test.
 *
 * Proves that on this machine the toolchain, the platform layer, the logger and the
 * OS error reporting all work. It prints an environment summary you can paste into
 * the report (docs/ENVIRONMENT.md) and exits 0 only if every check passes.
 *
 * Usage: hello [logfile]      (default log file: hello.log in the current directory)
 */

#include "log.h"
#include "platform.h"

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>

#if defined(__clang__)
#define COMPILER_ID "clang " __clang_version__
#elif defined(__GNUC__)
#define COMPILER_ID "gcc " __VERSION__
#elif defined(_MSC_VER)
#define COMPILER_ID "msvc"
#else
#define COMPILER_ID "unknown"
#endif

#ifndef BUILD_TYPE
#define BUILD_TYPE "unspecified"
#endif

#define TIMER_CALLS 1000000

int main(int argc, char **argv)
{
    const char *log_path = argc > 1 ? argv[1] : "hello.log";
    char env[256];
    char err_text[256];
    uint64_t t0;
    uint64_t t1;
    uint64_t prev;
    uint64_t min_step = UINT64_MAX;
    int failures = 0;
    int i;
    int code;

    if (log_init(log_path) != 0) {
        failures++;
    }

    LOG_INFO("hello: platform=%s pid=%lu", plat_name(), plat_getpid());

    plat_env_summary(env, sizeof env);
    LOG_INFO("os: %s", env);
    LOG_INFO("compiler: %s, build type: %s", COMPILER_ID, BUILD_TYPE);
    LOG_INFO("logical CPUs online: %d", plat_cpu_count());

    /* Timer: cost of one reading, and the smallest step the clock can show. */
    t0 = plat_now_ns();
    for (i = 0; i < TIMER_CALLS; i++) {
        (void)plat_now_ns();
    }
    t1 = plat_now_ns();
    LOG_INFO("timer: about %" PRIu64 " ns per plat_now_ns() call", (t1 - t0) / TIMER_CALLS);

    prev = plat_now_ns();
    for (i = 0; i < 10000; i++) {
        uint64_t t = plat_now_ns();
        if (t > prev && t - prev < min_step) {
            min_step = t - prev;
        }
        prev = t;
    }
    if (min_step == UINT64_MAX) {
        LOG_ERROR("timer: clock never advanced");
        failures++;
    } else {
        LOG_INFO("timer: smallest observed step %" PRIu64 " ns", min_step);
    }

    /* Error path: a call that must fail, and must produce a readable message. */
    if (plat_selftest_failing_call() == -1) {
        code = plat_last_error_code();
        plat_error_str(code, err_text, sizeof err_text);
        LOG_INFO("selftest: open of a missing path failed as expected: %s", err_text);
        if (code == 0 || err_text[0] == '\0') {
            LOG_ERROR("selftest: no OS error code or text was produced");
            failures++;
        }
    } else {
        LOG_ERROR("selftest: the call unexpectedly succeeded");
        failures++;
    }

    /* The macro every system-call wrapper will use (a WARN line is expected here). */
    if (plat_selftest_failing_call() == -1) {
        LOG_SYSWARN("selftest open (expected failure)");
    }

    if (failures == 0) {
        LOG_INFO("SMOKE TEST PASSED");
    } else {
        LOG_ERROR("SMOKE TEST FAILED (%d problem(s))", failures);
    }
    log_close();
    return failures == 0 ? 0 : 1;
}
