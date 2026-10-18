/*
 * demo_process_linux.c - fork(), exec() and wait() shown separately (Linux only).
 *
 * Windows has no fork(); there, CreateProcess() does the work of fork + exec. This
 * demo calls the raw Linux calls directly so each one can be seen on its own:
 *
 *   1. fork(): the child gets a copy of the parent's memory. A change made by the
 *      child is invisible to the parent (copy-on-write).
 *   2. fork() + execv(): three children replace themselves with child_helper.
 *   3. waitpid(): the parent collects each child's exit status.
 *   4. waitpid() with no children left fails with ECHILD.
 */

#include "demo_util.h"

#include <errno.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define CHILDREN 3

static int g_value = 100; /* changed by the child; must stay 100 in the parent */

int main(int argc, char **argv)
{
    int failures = 0;
    char helper[512];
    pid_t pids[CHILDREN];
    pid_t pid;
    int status;
    int i;

    demo_begin(argc, argv, "process_linux");
    demo_helper_path(argv[0], helper, sizeof helper);

    /* ---- 1. fork alone: copy-on-write ---- */
    pid = fork();
    if (pid < 0) {
        LOG_SYSERR("fork()");
        return demo_finish("process_linux", failures + 1);
    }
    if (pid == 0) {
        g_value = 200;
        _exit(g_value == 200 ? 42 : 1);
    }
    LOG_INFO("forked child pid %ld", (long)pid);
    DEMO_CHECK(failures, waitpid(pid, &status, 0) == pid, "wait for the forked child");
    DEMO_CHECK(failures, WIFEXITED(status) && WEXITSTATUS(status) == 42, "child saw its own changed copy (exit 42)");
    DEMO_CHECK(failures, g_value == 100, "parent's copy of the variable is unchanged (copy-on-write)");

    /* ---- 2. fork + exec ---- */
    for (i = 0; i < CHILDREN; i++) {
        pid = fork();
        if (pid < 0) {
            LOG_SYSERR("fork()");
            failures++;
            pids[i] = -1;
            continue;
        }
        if (pid == 0) {
            char code[16];
            char *child_argv[4];

            snprintf(code, sizeof code, "%d", 10 + i);
            child_argv[0] = helper;
            child_argv[1] = (char *)"exit";
            child_argv[2] = code;
            child_argv[3] = NULL;
            execv(helper, child_argv);
            _exit(127); /* only reached if exec failed */
        }
        pids[i] = pid;
    }

    /* ---- 3. wait ---- */
    for (i = 0; i < CHILDREN; i++) {
        char what[64];

        if (pids[i] < 0) {
            continue;
        }
        if (waitpid(pids[i], &status, 0) != pids[i]) {
            LOG_SYSERR("waitpid()");
            failures++;
            continue;
        }
        snprintf(what, sizeof what, "exec'd child %d exited with code %d", i, 10 + i);
        DEMO_CHECK(failures, WIFEXITED(status) && WEXITSTATUS(status) == 10 + i, what);
    }

    /* ---- 4. wait with nothing left to wait for ---- */
    LOG_INFO("waiting with no children left; the ERROR line that follows is expected");
    errno = 0;
    if (waitpid(-1, &status, 0) == -1) {
        LOG_SYSERR("waitpid(-1) with no children");
        DEMO_CHECK(failures, errno == ECHILD, "waitpid reports ECHILD when there is nothing to wait for");
    } else {
        DEMO_CHECK(failures, 0, "waitpid unexpectedly succeeded");
    }

    return demo_finish("process_linux", failures);
}
