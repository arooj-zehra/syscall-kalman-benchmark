/* Windows (Win32) implementation of platform.h. Built when WIN32 is set. */

#include "platform.h"

#include <windows.h>

#include <stdio.h>

const char *plat_name(void)
{
    return "windows";
}

uint64_t plat_now_ns(void)
{
    static uint64_t freq = 0;
    LARGE_INTEGER counter;
    uint64_t v;

    if (freq == 0) {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        freq = (uint64_t)f.QuadPart;
    }
    QueryPerformanceCounter(&counter);
    v = (uint64_t)counter.QuadPart;
    /* Split the division so the multiplication cannot overflow 64 bits. */
    return (v / freq) * 1000000000ull + (v % freq) * 1000000000ull / freq;
}

void plat_timestamp(char *buf, size_t n)
{
    SYSTEMTIME st;

    if (n == 0) {
        return;
    }
    GetLocalTime(&st);
    snprintf(buf, n, "%04d-%02d-%02d %02d:%02d:%02d.%03d",
             (int)st.wYear, (int)st.wMonth, (int)st.wDay,
             (int)st.wHour, (int)st.wMinute, (int)st.wSecond, (int)st.wMilliseconds);
}

unsigned long plat_getpid(void)
{
    return (unsigned long)GetCurrentProcessId();
}

int plat_cpu_count(void)
{
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    return si.dwNumberOfProcessors > 0 ? (int)si.dwNumberOfProcessors : 1;
}

void plat_env_summary(char *buf, size_t n)
{
    static const char *key = "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion";
    char build[32] = "?";
    DWORD build_size = (DWORD)sizeof build;
    DWORD ubr = 0;
    DWORD ubr_size = (DWORD)sizeof ubr;
    SYSTEM_INFO si;
    const char *arch;

    /* GetVersionEx() reports a fake version unless the exe is manifested, so read
     * the real build number from the registry instead. */
    if (RegGetValueA(HKEY_LOCAL_MACHINE, key, "CurrentBuildNumber", RRF_RT_REG_SZ,
                     NULL, build, &build_size) != ERROR_SUCCESS) {
        snprintf(build, sizeof build, "?");
    }
    if (RegGetValueA(HKEY_LOCAL_MACHINE, key, "UBR", RRF_RT_REG_DWORD,
                     NULL, &ubr, &ubr_size) != ERROR_SUCCESS) {
        ubr = 0;
    }

    GetNativeSystemInfo(&si);
    switch (si.wProcessorArchitecture) {
    case PROCESSOR_ARCHITECTURE_AMD64: arch = "x64"; break;
    case PROCESSOR_ARCHITECTURE_ARM64: arch = "arm64"; break;
    case PROCESSOR_ARCHITECTURE_INTEL: arch = "x86"; break;
    default:                           arch = "unknown-arch"; break;
    }

    snprintf(buf, n, "Windows build %s.%lu %s", build, (unsigned long)ubr, arch);
}

int plat_last_error_code(void)
{
    return (int)GetLastError();
}

void plat_error_str(int code, char *buf, size_t n)
{
    char text[256];
    DWORD len;

    len = FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                         NULL, (DWORD)code, 0, text, (DWORD)sizeof text, NULL);
    if (len == 0) {
        snprintf(buf, n, "error %d: (no system message)", code);
        return;
    }
    /* FormatMessage ends the text with "\r\n" and often a period; trim the line break. */
    while (len > 0 && (text[len - 1] == '\r' || text[len - 1] == '\n')) {
        text[--len] = '\0';
    }
    snprintf(buf, n, "error %d: %s", code, text);
}

int plat_selftest_failing_call(void)
{
    HANDLE h = CreateFileA("C:\\nonexistent-dir-for-selftest\\file", GENERIC_READ,
                           FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h != INVALID_HANDLE_VALUE) {
        CloseHandle(h);
        return 0;
    }
    return -1;
}
