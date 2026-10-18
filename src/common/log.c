#include "log.h"

#include "platform.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static FILE *g_file = NULL;
static LogLevel g_min_level = LOG_LEVEL_INFO;

static const char *level_name(LogLevel level)
{
    switch (level) {
    case LOG_LEVEL_DEBUG: return "DEBUG";
    case LOG_LEVEL_INFO:  return "INFO";
    case LOG_LEVEL_WARN:  return "WARN";
    case LOG_LEVEL_ERROR: return "ERROR";
    }
    return "?";
}

static const char *base_name(const char *path)
{
    const char *slash = strrchr(path, '/');
    const char *back = strrchr(path, '\\');
    if (back != NULL && (slash == NULL || back > slash)) {
        slash = back;
    }
    return slash != NULL ? slash + 1 : path;
}

int log_init(const char *path)
{
    if (g_file != NULL) {
        fclose(g_file);
        g_file = NULL;
    }
    if (path == NULL) {
        return 0;
    }
    g_file = fopen(path, "a");
    if (g_file == NULL) {
        fprintf(stderr, "log_init: cannot open '%s' for append; logging to stderr only\n", path);
        return -1;
    }
    return 0;
}

void log_close(void)
{
    if (g_file != NULL) {
        fclose(g_file);
        g_file = NULL;
    }
}

void log_set_level(LogLevel min_level)
{
    g_min_level = min_level;
}

void log_write(LogLevel level, const char *file, int line, const char *fmt, ...)
{
    char timestamp[40];
    char message[1024];
    char out[1400];
    va_list ap;

    if (level < g_min_level) {
        return;
    }

    plat_timestamp(timestamp, sizeof timestamp);

    va_start(ap, fmt);
    vsnprintf(message, sizeof message, fmt, ap);
    va_end(ap);

    snprintf(out, sizeof out, "%s [%-5s] [pid %lu] %s:%d  %s\n",
             timestamp, level_name(level), plat_getpid(), base_name(file), line, message);

    fputs(out, stderr);
    fflush(stderr);
    if (g_file != NULL) {
        fputs(out, g_file);
        fflush(g_file);
    }
}

void log_syserr_at(LogLevel level, const char *file, int line, const char *what)
{
    /* Read the error code first, before anything else can change it. */
    int code = plat_last_error_code();
    char text[256];

    plat_error_str(code, text, sizeof text);
    log_write(level, file, line, "%s failed: %s", what, text);
}
