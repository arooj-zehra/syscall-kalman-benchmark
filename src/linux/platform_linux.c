/* Linux / POSIX implementation of platform.h. Built when WIN32 is not set. */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "platform.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
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
