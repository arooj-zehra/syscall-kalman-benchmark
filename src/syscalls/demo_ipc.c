/*
 * demo_ipc.c - inter-process communication.
 *
 * Linux: pipe, shmget, shmat.
 * Windows: CreatePipe, CreateFileMapping, MapViewOfFile.
 *
 *   1. Pipes. The parent sends the numbers 1..100 to a child through one pipe. The
 *      child adds them up and sends the total back through a second pipe.
 *   2. Shared memory. The parent creates a segment, a child attaches it and fills it
 *      with a known pattern, and the parent checks the pattern.
 *   3. Forced error: attaching to a segment that does not exist.
 */

#include "demo_util.h"
#include "platform.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define SHM_NAME "sk_demo_shm"
#define SHM_LEN 4096u
#define COUNT 100u

static void demo_pipes(const char *helper, int *failures)
{
    plat_handle to_child_rd, to_child_wr; /* parent -> child */
    plat_handle to_parent_rd, to_parent_wr; /* child -> parent */
    plat_handle inherit[2];
    char arg_rd[32];
    char arg_wr[32];
    char *child_argv[5];
    plat_proc child;
    uint32_t values[COUNT];
    char reply[64];
    int64_t n;
    size_t used = 0;
    int exit_code = -1;
    uint32_t i;

    LOG_INFO("-- pipes: send 1..100 to a child, get the sum back");
    DEMO_CHECK(*failures, plat_pipe(&to_child_rd, &to_child_wr) == 0, "created the parent-to-child pipe");
    DEMO_CHECK(*failures, plat_pipe(&to_parent_rd, &to_parent_wr) == 0, "created the child-to-parent pipe");

    /* The child reads from to_child_rd and writes to to_parent_wr. Only those two are shared. */
    snprintf(arg_rd, sizeof arg_rd, "%lld", (long long)to_child_rd);
    snprintf(arg_wr, sizeof arg_wr, "%lld", (long long)to_parent_wr);
    child_argv[0] = (char *)helper;
    child_argv[1] = (char *)"pipe-sum";
    child_argv[2] = arg_rd;
    child_argv[3] = arg_wr;
    child_argv[4] = NULL;
    inherit[0] = to_child_rd;
    inherit[1] = to_parent_wr;

    if (plat_spawn(helper, child_argv, inherit, 2, &child) != 0) {
        (*failures)++;
        return;
    }
    /* Close the parent's copies of the child's ends, or the child would never see end-of-data. */
    (void)plat_close(to_child_rd);
    (void)plat_close(to_parent_wr);

    for (i = 0; i < COUNT; i++) {
        values[i] = i + 1;
    }
    n = plat_write(to_child_wr, values, sizeof values);
    DEMO_CHECK(*failures, n == (int64_t)sizeof values, "wrote 100 numbers into the pipe");
    (void)plat_close(to_child_wr); /* signals end of data to the child */

    while (used < sizeof reply - 1) {
        n = plat_read(to_parent_rd, reply + used, sizeof reply - 1 - used);
        if (n <= 0) {
            break;
        }
        used += (size_t)n;
    }
    reply[used] = '\0';
    (void)plat_close(to_parent_rd);

    DEMO_CHECK(*failures, plat_wait(child, &exit_code) == 0 && exit_code == 0, "child exited with code 0");
    DEMO_CHECK(*failures, atoi(reply) == 5050, "child's sum of 1..100 is 5050");
}

static void demo_shared_memory(const char *helper, int *failures)
{
    plat_shm shm;
    char len_arg[32];
    char *child_argv[5];
    plat_proc child;
    int exit_code = -1;
    unsigned char *p;
    size_t i;
    int match = 1;

    LOG_INFO("-- shared memory: a child fills a segment, the parent checks it");
    if (plat_shm_create(SHM_NAME, SHM_LEN, &shm) != 0) {
        DEMO_CHECK(*failures, 0, "created a shared memory segment");
        return;
    }
    DEMO_CHECK(*failures, shm.addr != NULL, "created and attached a shared memory segment");

    p = (unsigned char *)shm.addr;
    DEMO_CHECK(*failures, p[0] == 0 && p[SHM_LEN - 1] == 0, "new segment is zero-filled");

    snprintf(len_arg, sizeof len_arg, "%u", SHM_LEN);
    child_argv[0] = (char *)helper;
    child_argv[1] = (char *)"shm-fill";
    child_argv[2] = (char *)SHM_NAME;
    child_argv[3] = len_arg;
    child_argv[4] = NULL;

    if (plat_spawn(helper, child_argv, NULL, 0, &child) == 0) {
        DEMO_CHECK(*failures, plat_wait(child, &exit_code) == 0 && exit_code == 0, "child attached, filled and detached");
        for (i = 0; i < SHM_LEN; i++) {
            if (p[i] != DEMO_PATTERN(i)) {
                match = 0;
                break;
            }
        }
        DEMO_CHECK(*failures, match, "parent sees the pattern the child wrote");
    } else {
        DEMO_CHECK(*failures, 0, "started the shared-memory child");
    }

    DEMO_CHECK(*failures, plat_shm_destroy(&shm) == 0, "detached and removed the segment");

    LOG_INFO("attaching to the removed segment; the ERROR line that follows is expected");
    {
        plat_shm gone;
        DEMO_CHECK(*failures, plat_shm_attach(SHM_NAME, SHM_LEN, &gone) == -1,
                   "attaching to a segment that no longer exists fails");
    }
}

int main(int argc, char **argv)
{
    int failures = 0;
    char helper[512];

    demo_begin(argc, argv, "ipc");
    demo_helper_path(argv[0], helper, sizeof helper);

    demo_pipes(helper, &failures);
    demo_shared_memory(helper, &failures);

    return demo_finish("ipc", failures);
}
