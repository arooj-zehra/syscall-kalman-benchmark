/* Linux / POSIX implementation of platform.h. Built when WIN32 is not set. */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "platform.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/utsname.h>
#include <time.h>
#include <unistd.h>

const char *plat_name(void)
{
    return "linux";
}

uint64_t plat_now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

void plat_timestamp(char *buf, size_t n)
{
    struct timespec ts;
    struct tm tmv;
    size_t len;

    if (n == 0) {
        return;
    }
    clock_gettime(CLOCK_REALTIME, &ts);
    localtime_r(&ts.tv_sec, &tmv);
    len = strftime(buf, n, "%Y-%m-%d %H:%M:%S", &tmv);
    if (len == 0) {
        buf[0] = '\0';
        return;
    }
    snprintf(buf + len, n - len, ".%03ld", ts.tv_nsec / 1000000L);
}

unsigned long plat_getpid(void)
{
    return (unsigned long)getpid();
}

int plat_cpu_count(void)
{
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    return n > 0 ? (int)n : 1;
}

void plat_env_summary(char *buf, size_t n)
{
    struct utsname u;

    if (uname(&u) != 0) {
        snprintf(buf, n, "Linux (uname failed)");
        return;
    }
    /* WSL kernels carry "microsoft" / "WSL" in the release string. */
    snprintf(buf, n, "%s %s %s%s", u.sysname, u.release, u.machine,
             (strcasestr(u.release, "microsoft") != NULL || strcasestr(u.release, "wsl") != NULL)
                 ? " (WSL detected)"
                 : "");
}

int plat_last_error_code(void)
{
    return errno;
}

void plat_error_str(int code, char *buf, size_t n)
{
    snprintf(buf, n, "errno %d: %s", code, strerror(code));
}

int plat_selftest_failing_call(void)
{
    int fd = open("/nonexistent-dir-for-selftest/file", O_RDONLY);
    if (fd >= 0) {
        close(fd);
        return 0;
    }
    return -1;
}

int plat_usage_self(plat_usage *u)
{
    struct rusage ru;

    if (getrusage(RUSAGE_SELF, &ru) != 0) {
        return -1;
    }
    u->user_us = (uint64_t)ru.ru_utime.tv_sec * 1000000ull + (uint64_t)ru.ru_utime.tv_usec;
    u->sys_us = (uint64_t)ru.ru_stime.tv_sec * 1000000ull + (uint64_t)ru.ru_stime.tv_usec;
    u->peak_kb = (uint64_t)ru.ru_maxrss; /* kilobytes on Linux */
    return 0;
}

int plat_exe_path(char *buf, size_t n)
{
    ssize_t len;

    if (n < 2) {
        return -1;
    }
    len = readlink("/proc/self/exe", buf, n - 1);
    if (len < 0 || (size_t)len >= n - 1) {
        return -1;
    }
    buf[len] = '\0';
    return 0;
}

void plat_sleep_ms(unsigned ms)
{
    struct timespec ts;

    ts.tv_sec = (time_t)(ms / 1000u);
    ts.tv_nsec = (long)(ms % 1000u) * 1000000L;
    while (nanosleep(&ts, &ts) != 0 && errno == EINTR) {
        /* continue with the remaining time */
    }
}
