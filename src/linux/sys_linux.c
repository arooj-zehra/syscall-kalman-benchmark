/*
 * sys_linux.c - Linux implementation of the wrapped system calls in platform.h.
 *
 * Each wrapper is a thin layer over one Linux call (or a small fixed sequence for
 * plat_spawn). On failure it logs the call, its arguments and the errno text, then
 * returns -1 / NULL with errno preserved. Success paths do no logging and no
 * allocation, so benchmarks measure the call itself.
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "platform.h"

#include "log.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/mman.h>
#include <sys/shm.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

/* Log "<what> failed: <errno text>" and leave errno exactly as the failing call set it. */
static void syserr(const char *fmt, ...) LOG_PRINTF_FMT(1, 2);

static void syserr(const char *fmt, ...)
{
    int saved = errno;
    char what[256];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(what, sizeof what, fmt, ap);
    va_end(ap);

    errno = saved;
    log_syserr_at(LOG_LEVEL_ERROR, __FILE__, __LINE__, what);
    errno = saved;
}

/* ======================= File I/O: open read write close ================== */

plat_handle plat_open(const char *path, int flags, unsigned mode)
{
    int want_read = (flags & PLAT_OPEN_READ) != 0;
    int want_write = (flags & PLAT_OPEN_WRITE) != 0;
    int oflags;
    int fd;

    if (want_read && want_write) {
        oflags = O_RDWR;
    } else if (want_write) {
        oflags = O_WRONLY;
    } else {
        oflags = O_RDONLY;
    }
    if (flags & PLAT_OPEN_CREATE) {
        oflags |= O_CREAT;
    }
    if (flags & PLAT_OPEN_TRUNC) {
        oflags |= O_TRUNC;
    }
    oflags |= O_CLOEXEC; /* not inherited by children unless plat_spawn() says so */

    fd = open(path, oflags, (mode_t)mode);
    if (fd < 0) {
        syserr("open(%s, flags=0x%x)", path, (unsigned)flags);
        return PLAT_INVALID_HANDLE;
    }
    return (plat_handle)fd;
}

int64_t plat_read(plat_handle h, void *buf, size_t len)
{
    ssize_t n;

    do {
        n = read((int)h, buf, len);
    } while (n < 0 && errno == EINTR);

    if (n < 0) {
        syserr("read(fd=%lld, len=%zu)", (long long)h, len);
        return -1;
    }
    return (int64_t)n;
}

int64_t plat_write(plat_handle h, const void *buf, size_t len)
{
    ssize_t n;

    do {
        n = write((int)h, buf, len);
    } while (n < 0 && errno == EINTR);

    if (n < 0) {
        syserr("write(fd=%lld, len=%zu)", (long long)h, len);
        return -1;
    }
    return (int64_t)n;
}

int plat_close(plat_handle h)
{
    if (close((int)h) != 0) {
        syserr("close(fd=%lld)", (long long)h);
        return -1;
    }
    return 0;
}

int plat_unlink(const char *path)
{
    if (unlink(path) != 0) {
        syserr("unlink(%s)", path);
        return -1;
    }
    return 0;
}

int plat_stat_path(const char *path, plat_stat *st)
{
    struct stat sb;

    if (stat(path, &sb) != 0) {
        syserr("stat(%s)", path);
        return -1;
    }
    st->size = (int64_t)sb.st_size;
    st->mode = (unsigned)(sb.st_mode & 07777);
    st->uid = (long)sb.st_uid;
    st->gid = (long)sb.st_gid;
    return 0;
}

/* ======================= Process: fork exec wait ========================== */

int plat_spawn(const char *exe, char *const argv[],
               const plat_handle *inherit, int n_inherit, plat_proc *out)
{
    int errpipe[2];
    pid_t pid;
    int child_errno = 0;
    ssize_t n;
    int i;

    /* A close-on-exec pipe tells the parent whether exec() worked: if exec succeeds
     * the pipe closes with nothing written; if it fails the child writes errno. */
    if (pipe2(errpipe, O_CLOEXEC) != 0) {
        syserr("pipe2(spawn status pipe)");
        return -1;
    }

    pid = fork();
    if (pid < 0) {
        syserr("fork()");
        close(errpipe[0]);
        close(errpipe[1]);
        return -1;
    }

    if (pid == 0) {
        /* Child. Keep only the handles the caller asked to share. */
        for (i = 0; i < n_inherit; i++) {
            int fd = (int)inherit[i];
            int fl = fcntl(fd, F_GETFD);
            if (fl >= 0) {
                (void)fcntl(fd, F_SETFD, fl & ~FD_CLOEXEC);
            }
        }
        execv(exe, argv);
        {
            int e = errno;
            ssize_t w = write(errpipe[1], &e, sizeof e);
            (void)w;
        }
        _exit(127);
    }

    close(errpipe[1]);
    do {
        n = read(errpipe[0], &child_errno, sizeof child_errno);
    } while (n < 0 && errno == EINTR);
    close(errpipe[0]);

    if (n == (ssize_t)sizeof child_errno) {
        int status;
        (void)waitpid(pid, &status, 0); /* reap the child that never started */
        errno = child_errno;
        syserr("exec(%s)", exe);
        errno = child_errno;
        return -1;
    }

    *out = (plat_proc)pid;
    return 0;
}

int plat_wait(plat_proc p, int *exit_code)
{
    int status = 0;
    pid_t r;

    do {
        r = waitpid((pid_t)p, &status, 0);
    } while (r < 0 && errno == EINTR);

    if (r < 0) {
        syserr("waitpid(pid=%lld)", (long long)p);
        return -1;
    }
    if (WIFEXITED(status)) {
        *exit_code = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        *exit_code = 128 + WTERMSIG(status);
    } else {
        *exit_code = -1;
    }
    return 0;
}

/* ======================= Memory: mmap munmap ============================== */

void *plat_map_anon(size_t len)
{
    void *p = mmap(NULL, len, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

    if (p == MAP_FAILED) {
        syserr("mmap(anonymous, len=%zu)", len);
        return NULL;
    }
    return p;
}

int plat_unmap_anon(void *addr, size_t len)
{
    if (munmap(addr, len) != 0) {
        syserr("munmap(%p, len=%zu)", addr, len);
        return -1;
    }
    return 0;
}

void *plat_map_file(plat_handle h, size_t len, int writable)
{
    int prot = PROT_READ | (writable ? PROT_WRITE : 0);
    void *p = mmap(NULL, len, prot, MAP_SHARED, (int)h, 0);

    if (p == MAP_FAILED) {
        syserr("mmap(fd=%lld, len=%zu, %s)", (long long)h, len, writable ? "read-write" : "read-only");
        return NULL;
    }
    return p;
}

int plat_unmap_file(void *addr, size_t len)
{
    if (munmap(addr, len) != 0) {
        syserr("munmap(%p, len=%zu)", addr, len);
        return -1;
    }
    return 0;
}

/* ======================= IPC: pipe shmget shmat =========================== */

int plat_pipe(plat_handle *rd, plat_handle *wr)
{
    static int sigpipe_ignored = 0;
    int fds[2];

    /* Match Windows: writing to a pipe whose reader has gone fails with an error
     * (EPIPE) that the caller can handle, instead of killing the process with
     * SIGPIPE. Done once, the first time a pipe is made. */
    if (!sigpipe_ignored) {
        (void)signal(SIGPIPE, SIG_IGN);
        sigpipe_ignored = 1;
    }
    if (pipe2(fds, O_CLOEXEC) != 0) {
        syserr("pipe()");
        return -1;
    }
    *rd = (plat_handle)fds[0];
    *wr = (plat_handle)fds[1];
    return 0;
}

/* Name -> System V key. FNV-1a, forced into 1..0x7fffffff (0 is IPC_PRIVATE). */
static key_t shm_key(const char *name)
{
    uint32_t h = 2166136261u;

    for (; *name != '\0'; name++) {
        h ^= (unsigned char)*name;
        h *= 16777619u;
    }
    h &= 0x7fffffffu;
    if (h == 0) {
        h = 1;
    }
    return (key_t)h;
}

int plat_shm_create(const char *name, size_t len, plat_shm *shm)
{
    key_t key = shm_key(name);
    int id = shmget(key, len, IPC_CREAT | IPC_EXCL | 0600);
    void *addr;

    if (id < 0 && errno == EEXIST) {
        /* A segment with this key survived a crashed run. Remove it and retry once. */
        int old = shmget(key, 0, 0600);
        if (old >= 0) {
            LOG_WARN("shm '%s': removing stale segment (shmid %d) left by an earlier run", name, old);
            (void)shmctl(old, IPC_RMID, NULL);
        }
        id = shmget(key, len, IPC_CREAT | IPC_EXCL | 0600);
    }
    if (id < 0) {
        syserr("shmget(name=%s, key=0x%x, len=%zu)", name, (unsigned)key, len);
        return -1;
    }

    addr = shmat(id, NULL, 0);
    if (addr == (void *)-1) {
        syserr("shmat(shmid=%d)", id);
        (void)shmctl(id, IPC_RMID, NULL);
        return -1;
    }

    shm->addr = addr;
    shm->len = len;
    shm->id = (intptr_t)id;
    shm->owner = 1;
    return 0;
}

int plat_shm_attach(const char *name, size_t len, plat_shm *shm)
{
    key_t key = shm_key(name);
    int id = shmget(key, 0, 0600);
    void *addr;

    if (id < 0) {
        syserr("shmget(name=%s, key=0x%x) [attach existing]", name, (unsigned)key);
        return -1;
    }
    addr = shmat(id, NULL, 0);
    if (addr == (void *)-1) {
        syserr("shmat(shmid=%d)", id);
        return -1;
    }

    shm->addr = addr;
    shm->len = len;
    shm->id = (intptr_t)id;
    shm->owner = 0;
    return 0;
}

int plat_shm_detach(plat_shm *shm)
{
    if (shm->addr == NULL) {
        return 0;
    }
    if (shmdt(shm->addr) != 0) {
        syserr("shmdt(%p)", shm->addr);
        return -1;
    }
    shm->addr = NULL;
    return 0;
}

int plat_shm_destroy(plat_shm *shm)
{
    int rc = 0;

    if (plat_shm_detach(shm) != 0) {
        rc = -1;
    }
    if (shm->owner) {
        /* IPC_RMID removes the segment once the last process has detached. */
        if (shmctl((int)shm->id, IPC_RMID, NULL) != 0) {
            syserr("shmctl(shmid=%lld, IPC_RMID)", (long long)shm->id);
            rc = -1;
        }
        shm->owner = 0;
    }
    return rc;
}

/* ======================= Permissions: chmod chown umask =================== */

int plat_chmod(const char *path, unsigned mode)
{
    if (chmod(path, (mode_t)(mode & 07777)) != 0) {
        syserr("chmod(%s, 0%o)", path, mode & 07777);
        return -1;
    }
    return 0;
}

int plat_chown(const char *path, long uid, long gid)
{
    /* (uid_t)-1 and (gid_t)-1 mean "leave unchanged", matching the header comment. */
    if (chown(path, (uid_t)uid, (gid_t)gid) != 0) {
        syserr("chown(%s, uid=%ld, gid=%ld)", path, uid, gid);
        return -1;
    }
    return 0;
}

unsigned plat_umask(unsigned mask)
{
    /* umask() cannot fail. */
    return (unsigned)umask((mode_t)(mask & 0777));
}

long plat_getuid(void)
{
    return (long)getuid();
}

long plat_getgid(void)
{
    return (long)getgid();
}
