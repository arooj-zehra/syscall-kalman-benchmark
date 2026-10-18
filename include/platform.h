/*
 * platform.h - the single boundary between portable code and the operating system.
 *
 * Rule: code outside src/linux/ and src/win/ never includes <windows.h> or
 * POSIX-only headers and never contains #ifdef _WIN32. Everything it needs from
 * the OS is declared here. Linux implements these in src/linux/platform_linux.c,
 * Windows in src/win/platform_win.c.
 *
 * Phase 1 only needs the basics below. Later phases add the wrapped system
 * calls (file I/O, process, memory, IPC, permissions) to this header.
 */
#ifndef PLATFORM_H
#define PLATFORM_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* "linux" or "windows". */
const char *plat_name(void);

/* Monotonic clock in nanoseconds. Only differences are meaningful.
 * Linux: clock_gettime(CLOCK_MONOTONIC). Windows: QueryPerformanceCounter. */
uint64_t plat_now_ns(void);

/* Local wall-clock time as "YYYY-MM-DD HH:MM:SS.mmm" (needs n >= 24). */
void plat_timestamp(char *buf, size_t n);

/* Process id of the calling process. */
unsigned long plat_getpid(void);

/* Number of logical CPUs currently online. */
int plat_cpu_count(void);

/* One-line description of the OS, e.g. "Linux 5.15.x x86_64 (WSL detected)"
 * or "Windows build 19045.7725 x64". Paste it into the report. */
void plat_env_summary(char *buf, size_t n);

/* Last OS error of the calling thread: errno on Linux, GetLastError() on
 * Windows. Read it immediately after the failing call. */
int plat_last_error_code(void);

/* Human-readable text for an OS error code, e.g.
 * "errno 2: No such file or directory". */
void plat_error_str(int code, char *buf, size_t n);

/* Smoke-test helper: opens a path that cannot exist (open() / CreateFileA()).
 * Returns -1 if the call failed as expected, 0 if it unexpectedly succeeded.
 * After a -1 the OS error code is available through plat_last_error_code(). */
int plat_selftest_failing_call(void);

#ifdef __cplusplus
}
#endif

#endif /* PLATFORM_H */
