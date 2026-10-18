/*
 * child_helper.c - the program the process and IPC demos start.
 *
 * It is a normal portable program built on platform.h, so the same source runs as
 * the "exec" target on Linux and the CreateProcess target on Windows.
 *
 *   child_helper exit <code>                 exit with <code>
 *   child_helper pipe-sum <rd> <wr>          read uint32 values from handle <rd> until EOF,
 *                                            write their sum as decimal text to handle <wr>
 *   child_helper shm-fill <name> <len>       attach shared memory <name> and fill it with
 *                                            the DEMO_PATTERN bytes
 *
 * Handle arguments are the numeric handle values passed down by plat_spawn().
 */

#include "demo_util.h"
#include "platform.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static int mode_exit(int argc, char **argv)
{
    return argc >= 3 ? atoi(argv[2]) : 2;
}

static int mode_pipe_sum(int argc, char **argv)
{
    plat_handle rd;
    plat_handle wr;
    unsigned char buf[512];
    unsigned char word[4];
    size_t word_len = 0;
    uint64_t sum = 0;
    char out[32];
    int len;

    if (argc < 4) {
        return 2;
    }
    rd = (plat_handle)strtoll(argv[2], NULL, 10);
    wr = (plat_handle)strtoll(argv[3], NULL, 10);

    for (;;) {
        int64_t n = plat_read(rd, buf, sizeof buf);
        int64_t i;

        if (n < 0) {
            return 3;
        }
        if (n == 0) {
            break; /* every write end is closed: end of data */
        }
        for (i = 0; i < n; i++) {
            word[word_len++] = buf[i];
            if (word_len == sizeof word) {
                uint32_t v;
                memcpy(&v, word, sizeof v);
                sum += v;
                word_len = 0;
            }
        }
    }

    len = snprintf(out, sizeof out, "%llu", (unsigned long long)sum);
    if (plat_write(wr, out, (size_t)len) != len) {
        return 4;
    }
    (void)plat_close(rd);
    (void)plat_close(wr);
    return word_len == 0 ? 0 : 5;
}

static int mode_shm_fill(int argc, char **argv)
{
    plat_shm shm;
    unsigned char *p;
    size_t i;

    if (argc < 4) {
        return 2;
    }
    if (plat_shm_attach(argv[2], (size_t)strtoull(argv[3], NULL, 10), &shm) != 0) {
        return 3;
    }
    p = (unsigned char *)shm.addr;
    for (i = 0; i < shm.len; i++) {
        p[i] = DEMO_PATTERN(i);
    }
    return plat_shm_detach(&shm) == 0 ? 0 : 4;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: child_helper exit|pipe-sum|shm-fill ...\n");
        return 2;
    }
    if (strcmp(argv[1], "exit") == 0) {
        return mode_exit(argc, argv);
    }
    if (strcmp(argv[1], "pipe-sum") == 0) {
        return mode_pipe_sum(argc, argv);
    }
    if (strcmp(argv[1], "shm-fill") == 0) {
        return mode_shm_fill(argc, argv);
    }
    fprintf(stderr, "child_helper: unknown mode '%s'\n", argv[1]);
    return 2;
}
