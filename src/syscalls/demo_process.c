/*
 * demo_process.c - starting and waiting for child processes, portable version.
 *
 * Linux: plat_spawn = fork + exec, plat_wait = waitpid.
 * Windows: plat_spawn = CreateProcess, plat_wait = WaitForSingleObject + GetExitCodeProcess.
 *
 * Starts three children at once, each exiting with a different code, waits for all
 * of them, and checks the codes. Also checks that starting a missing program is
 * reported as a failure of plat_spawn itself.
 */

#include "demo_util.h"
#include "platform.h"

#include <stdio.h>

#define CHILDREN 3

int main(int argc, char **argv)
{
    int failures = 0;
    char helper[512];
    plat_proc procs[CHILDREN];
    int started = 0;
    int i;

    demo_begin(argc, argv, "process");
    demo_helper_path(argv[0], helper, sizeof helper);
    LOG_INFO("helper program: %s", helper);

    for (i = 0; i < CHILDREN; i++) {
        char code[16];
        char *child_argv[4];

        snprintf(code, sizeof code, "%d", 3 + i);
        child_argv[0] = helper;
        child_argv[1] = (char *)"exit";
        child_argv[2] = code;
        child_argv[3] = NULL;

        if (plat_spawn(helper, child_argv, NULL, 0, &procs[i]) == 0) {
            started++;
        }
    }
    DEMO_CHECK(failures, started == CHILDREN, "started 3 child processes");

    for (i = 0; i < started; i++) {
        int exit_code = -1;
        char what[64];

        DEMO_CHECK(failures, plat_wait(procs[i], &exit_code) == 0, "wait for a child");
        snprintf(what, sizeof what, "child %d exited with code %d", i, 3 + i);
        DEMO_CHECK(failures, exit_code == 3 + i, what);
    }

    LOG_INFO("starting a program that does not exist; the ERROR line that follows is expected");
    {
        char *bad_argv[2];
        plat_proc p;

        bad_argv[0] = (char *)"no_such_program";
        bad_argv[1] = NULL;
        DEMO_CHECK(failures, plat_spawn("./no_such_program_xyz", bad_argv, NULL, 0, &p) == -1,
                   "starting a missing program fails in plat_spawn");
    }

    return demo_finish("process", failures);
}
