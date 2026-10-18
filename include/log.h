/*
 * log.h - small logging facility used by every module and every system-call wrapper.
 *
 * Each line goes to stderr and, if log_init() was given a path, to that file:
 *
 *   2026-10-18 12:00:00.123 [ERROR] [pid 4242] hello.c:57  open failed: errno 2: ...
 *
 * The pid is in every line because in Phase 6 the master and the worker processes
 * append to the same log file. Each line is flushed immediately.
 */
#ifndef LOG_H
#define LOG_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    LOG_LEVEL_DEBUG = 0,
    LOG_LEVEL_INFO  = 1,
    LOG_LEVEL_WARN  = 2,
    LOG_LEVEL_ERROR = 3
} LogLevel;

/* Open (append) a log file. Returns 0 on success. On failure (or path == NULL)
 * logging continues to stderr only and -1 is returned for a failed open. */
int  log_init(const char *path);
void log_close(void);
void log_set_level(LogLevel min_level);

#if defined(__GNUC__) && !defined(__MINGW32__)
#define LOG_PRINTF_FMT(fmt_idx, first_arg) __attribute__((format(printf, fmt_idx, first_arg)))
#else
#define LOG_PRINTF_FMT(fmt_idx, first_arg)
#endif

void log_write(LogLevel level, const char *file, int line, const char *fmt, ...)
    LOG_PRINTF_FMT(4, 5);

/* Logs "<what> failed: <OS error text>" using the current OS error code. */
void log_syserr_at(LogLevel level, const char *file, int line, const char *what);

#define LOG_DEBUG(...) log_write(LOG_LEVEL_DEBUG, __FILE__, __LINE__, __VA_ARGS__)
#define LOG_INFO(...)  log_write(LOG_LEVEL_INFO,  __FILE__, __LINE__, __VA_ARGS__)
#define LOG_WARN(...)  log_write(LOG_LEVEL_WARN,  __FILE__, __LINE__, __VA_ARGS__)
#define LOG_ERROR(...) log_write(LOG_LEVEL_ERROR, __FILE__, __LINE__, __VA_ARGS__)

/* Use directly after a failing system call, before any other call that could
 * overwrite the OS error code:
 *     if (fd < 0) { LOG_SYSERR("open(data.bin)"); return -1; }
 * LOG_SYSWARN is the same at WARN level, for failures that are expected. */
#define LOG_SYSERR(what)  log_syserr_at(LOG_LEVEL_ERROR, __FILE__, __LINE__, (what))
#define LOG_SYSWARN(what) log_syserr_at(LOG_LEVEL_WARN,  __FILE__, __LINE__, (what))

#ifdef __cplusplus
}
#endif

#endif /* LOG_H */
