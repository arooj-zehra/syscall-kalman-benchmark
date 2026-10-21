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

/* =========================================================================
 * Phase 2+: wrapped system calls
 *
 * Conventions (all groups):
 *   - Integer-returning functions return 0 on success and -1 on failure,
 *     unless stated otherwise. Pointer-returning functions return NULL on
 *     failure. Handles use PLAT_INVALID_HANDLE.
 *   - A failure is logged (LOG_ERROR, with the call, its arguments and the OS
 *     error text) before returning, and the OS error code is preserved, so a
 *     caller can still read it with plat_last_error_code().
 *   - Success is silent, so the benchmark loops measure the call, not logging.
 *
 * Implemented in src/linux/sys_linux.c (Phase 2) and src/win/sys_win.c (Phase 3).
 * Each wrapper is documented with the Linux call and the Windows call(s) it
 * stands for; see docs/PLATFORM_API.md for the full table.
 * ========================================================================= */

/* A file descriptor on Linux, a HANDLE on Windows, widened to one integer type
 * so it can also be passed to a child process as a decimal string. */
typedef intptr_t plat_handle;
#define PLAT_INVALID_HANDLE ((plat_handle)-1)

/* ---- File I/O -----------------------------------------------------------
 * open() / CreateFile(), read() / ReadFile(), write() / WriteFile(),
 * close() / CloseHandle(). Handles are NOT inherited by child processes
 * unless passed to plat_spawn() explicitly. */
enum {
    PLAT_OPEN_READ   = 1,
    PLAT_OPEN_WRITE  = 2,
    PLAT_OPEN_CREATE = 4,  /* create the file if it does not exist */
    PLAT_OPEN_TRUNC  = 8   /* discard existing contents */
};

/* mode = permission bits for a newly created file (e.g. 0644); ignored on
 * Windows. */
plat_handle plat_open(const char *path, int flags, unsigned mode);

/* Bytes read (0 at end of file) or -1. May return fewer bytes than asked. */
int64_t plat_read(plat_handle h, void *buf, size_t len);

/* Bytes written or -1. May return fewer bytes than asked. */
int64_t plat_write(plat_handle h, const void *buf, size_t len);

int plat_close(plat_handle h);

/* Helpers needed around the 15 calls: unlink() / DeleteFile(), and
 * stat() / GetFileAttributesEx() + security descriptor. */
int plat_unlink(const char *path);

typedef struct {
    int64_t size;     /* bytes */
    unsigned mode;    /* permission bits, e.g. 0644 */
    long uid;         /* owner. Windows: 0 = Administrators, 1000 = current user, -1 = anyone else */
    long gid;         /* group. Windows: always -1 */
} plat_stat;

int plat_stat_path(const char *path, plat_stat *st);

/* ---- Process -------------------------------------------------------------
 * Linux: fork() + exec() + wait(). Windows: CreateProcess() +
 * WaitForSingleObject(). Windows has no fork(), so the portable pair is
 * "start a program" and "wait for it". The Linux-only demo
 * (src/syscalls/demo_process_linux.c) shows fork, exec and wait separately. */
typedef intptr_t plat_proc;

/* Start the program at `exe` with the NULL-terminated argument vector `argv`
 * (argv[0] is the program name). Handles listed in `inherit` are made
 * available to the child under the same numeric value; all other handles are
 * closed in the child. Returns -1 if the program could not be started (for
 * example, the file does not exist), so a bad path is reported here and not as
 * a mysterious exit code. */
int plat_spawn(const char *exe, char *const argv[],
               const plat_handle *inherit, int n_inherit, plat_proc *out);

/* Wait for the child to end and fetch its exit code. If the child was killed
 * by a signal (Linux) the code is 128 + signal number. Each child must be
 * waited for exactly once: this also releases it (reaps the zombie on Linux,
 * closes the process handle on Windows). */
int plat_wait(plat_proc p, int *exit_code);

/* ---- Memory --------------------------------------------------------------
 * Anonymous memory: mmap(MAP_ANONYMOUS) / VirtualAlloc, released by
 * munmap() / VirtualFree. File mapping: mmap(fd) / CreateFileMapping +
 * MapViewOfFile, released by munmap() / UnmapViewOfFile. Mapped memory is
 * page-aligned and anonymous memory is zero-filled. */
void *plat_map_anon(size_t len);
int   plat_unmap_anon(void *addr, size_t len);

/* Map the first `len` bytes of an open file. A writable mapping is shared:
 * changes reach the file. The file must be at least `len` bytes long. */
void *plat_map_file(plat_handle h, size_t len, int writable);
int   plat_unmap_file(void *addr, size_t len);

/* ---- IPC -----------------------------------------------------------------
 * Pipe: pipe() / CreatePipe(); use plat_read, plat_write and plat_close on
 * the two ends. Both ends are private to this process until passed to
 * plat_spawn().
 * Writing to a pipe whose read end is closed returns -1 (Linux: SIGPIPE is
 * ignored once the first pipe is made), so a vanished peer is an error the
 * caller can handle, as on Windows.
 *
 * Shared memory: shmget() + shmat() / CreateFileMapping (named) +
 * MapViewOfFile. A segment is identified by `name`; the creating process
 * must call plat_shm_destroy() when finished. A leftover segment of the same
 * name from a crashed run is removed (and a warning logged) on creation. */
int plat_pipe(plat_handle *rd, plat_handle *wr);

typedef struct {
    void    *addr;   /* start of the shared region, NULL when detached */
    size_t   len;
    intptr_t id;     /* shmid on Linux, mapping HANDLE on Windows */
    int      owner;  /* 1 if created by this process */
} plat_shm;

int plat_shm_create(const char *name, size_t len, plat_shm *shm);
int plat_shm_attach(const char *name, size_t len, plat_shm *shm); /* existing segment */
int plat_shm_detach(plat_shm *shm);                                /* shmdt */
int plat_shm_destroy(plat_shm *shm);                               /* creator: detach and remove */

/* ---- Permissions ---------------------------------------------------------
 * chmod() / SetFileSecurity (DACL), chown() / SetFileSecurity (owner),
 * umask() / no Win32 equivalent.
 *
 * Windows has ACLs, not mode bits, so the Windows wrappers translate:
 *   - plat_chmod writes a DACL with an owner entry, a BUILTIN\Users entry (the
 *     "group" class) and an Everyone entry; plat_stat_path reads the mode back
 *     from those entries.
 *     (The owner entry always keeps the right to change permissions and delete.)
 *   - plat_umask is emulated: plat_open applies (mode & ~umask) to files it
 *     creates once plat_umask has been called.
 *   - Users are pseudo ids: plat_getuid() is 1000, uid 0 means the
 *     Administrators group, and other numbers are rejected. Only a privileged
 *     process may give a file to uid 0. */
int      plat_chmod(const char *path, unsigned mode);
int      plat_chown(const char *path, long uid, long gid);  /* -1 leaves a field unchanged */
unsigned plat_umask(unsigned mask);                         /* sets the mask, returns the old one */
long     plat_getuid(void);
long     plat_getgid(void);

/* ---- Resource usage (Phase 4) --------------------------------------------
 * Used by the benchmark harness to report CPU time and memory next to the
 * timings. Linux: getrusage(RUSAGE_SELF). Windows: GetProcessTimes() and
 * GetProcessMemoryInfo().
 *
 * user_us and sys_us are CPU time used by this process since it started
 * (all threads); subtract two readings to get a delta. Resolution is coarse:
 * Windows accounts CPU time in scheduler ticks (about 15.6 ms), so only deltas
 * over runs of a second or more are meaningful. peak_kb is the process's
 * high-water mark of resident memory (Linux ru_maxrss, Windows
 * PeakWorkingSetSize); it never decreases. */
typedef struct {
    uint64_t user_us;
    uint64_t sys_us;
    uint64_t peak_kb;
} plat_usage;

int plat_usage_self(plat_usage *u);

/* Full path of the running executable (used to start a copy of it). */
int plat_exe_path(char *buf, size_t n);

/* Sleep without using the CPU. nanosleep() / Sleep(). */
void plat_sleep_ms(unsigned ms);

#ifdef __cplusplus
}
#endif

#endif /* PLATFORM_H */
