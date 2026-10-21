/*
 * kf_master - the parallel Kalman filter.
 *
 *   kf_master --in data/meas.bin --workers W [--repeat R] [--out FILE]
 *             [--expect-file data/est_serial.bin] [--csv results/scaling.csv]
 *             [--inject-failure start|compute]
 *
 * How the pieces fit (see docs/KALMAN_DESIGN.md):
 *
 *   input file --mmap--> every worker (read-only, shared page-cache pages)
 *   shared memory segment <--- every worker writes its tracks' estimates in place
 *   two pipes per worker carry 40-byte control messages: master->worker commands,
 *   worker->master results. No data goes through the pipes.
 *
 *   1. map the input header, create the shared output segment, create the pipes
 *   2. start W copies of kf_worker with plat_spawn; wait until all report READY
 *   3. R rounds: send every worker its block of tracks (GO), wait for every DONE
 *   4. send STOP, wait for the workers to exit
 *   5. hash the shared segment; compare with the serial result; optionally write it
 *
 * The result only counts if its hash equals the serial run's hash: a run that
 * does not match is reported as a failure and writes no CSV row.
 * Whatever goes wrong, the shared segment is removed and no worker is left running.
 */

#include "kalman.h"
#include "kfio.h"
#include "kfproto.h"
#include "log.h"
#include "platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    plat_proc   proc;
    int         started;
    plat_handle cmd_w;   /* master writes commands here */
    plat_handle res_r;   /* master reads results here */
} worker_slot;

static int usage(void)
{
    fprintf(stderr, "usage: kf_master --in FILE --workers W [--repeat R] [--out FILE] "
                    "[--expect-file FILE] [--csv FILE] [--inject-failure start|compute]\n");
    return 2;
}

static double ms(uint64_t ns)
{
    return (double)ns / 1e6;
}

static void close_handle(plat_handle *h)
{
    if (*h != PLAT_INVALID_HANDLE) {
        (void)plat_close(*h);
        *h = PLAT_INVALID_HANDLE;
    }
}

/* Wait for one worker's next message of the wanted type; any other outcome is a failure. */
static int expect(worker_slot *w, int index, int type, kf_msg *m)
{
    int got = kf_msg_recv(w->res_r, m);

    if (got == 0) {
        LOG_ERROR("worker %d closed its pipe before sending %s (it died)", index,
                  type == KF_MSG_READY ? "READY" : "DONE");
        return -1;
    }
    if (got < 0) {
        LOG_ERROR("worker %d: cannot read its message", index);
        return -1;
    }
    if (m->type == KF_MSG_ERROR) {
        LOG_ERROR("worker %d reported error %llu", index, (unsigned long long)m->a);
        return -1;
    }
    if (m->type != (uint32_t)type) {
        LOG_ERROR("worker %d sent message type %u, expected %d", index, (unsigned)m->type, type);
        return -1;
    }
    return 0;
}

int main(int argc, char **argv)
{
    const char *in = NULL;
    const char *out = NULL;
    const char *expect_file = NULL;
    const char *csv = NULL;
    const char *inject = NULL;
    int workers = 0;
    int repeat = 1;
    int i;
    int rc = 1;
    int failed = 0;
    kf_reader r;
    kf_gen_params p;
    plat_shm shm;
    worker_slot *slot = NULL;
    double *round_ms = NULL;
    double *cpu_round = NULL;
    uint64_t n_tracks;
    uint64_t n_steps;
    size_t per_track;
    size_t out_doubles;
    uint64_t t_start;
    uint64_t t_loaded;
    uint64_t t_up;
    uint64_t t_hash0;
    uint64_t t_hash;
    uint64_t t_write = 0;
    uint64_t hash = 0;
    uint64_t worker_peak_kb = 0;
    double imbalance_sum = 0.0;
    int spawned = 0;
    int shm_made = 0;
    int r_ok = 0;
    char exe[1024];
    char shm_name[64];
    kf_run_stats st;
    plat_usage mu;

    log_init(NULL);
    for (i = 1; i < argc; i++) {
        const char *a = argv[i];

        if (i + 1 >= argc) {
            return usage();
        }
        if (strcmp(a, "--in") == 0) {
            in = argv[++i];
        } else if (strcmp(a, "--out") == 0) {
            out = argv[++i];
        } else if (strcmp(a, "--csv") == 0) {
            csv = argv[++i];
        } else if (strcmp(a, "--expect-file") == 0) {
            expect_file = argv[++i];
        } else if (strcmp(a, "--inject-failure") == 0) {
            inject = argv[++i];
            if (strcmp(inject, "start") != 0 && strcmp(inject, "compute") != 0) {
                return usage();
            }
        } else if (strcmp(a, "--workers") == 0) {
            workers = atoi(argv[++i]);
        } else if (strcmp(a, "--repeat") == 0) {
            repeat = atoi(argv[++i]);
        } else {
            return usage();
        }
    }
    if (in == NULL || workers < 1 || workers > 256 || repeat < 1 || repeat > 1000) {
        return usage();
    }

    memset(&shm, 0, sizeof shm);
    memset(&st, 0, sizeof st);
    t_start = plat_now_ns();
    if (kf_reader_open(&r, in, KF_MAGIC_MEAS, 1) != 0) {
        return 1;
    }
    t_loaded = plat_now_ns();
    n_tracks = r.hdr.f.n_tracks;
    n_steps = r.hdr.f.n_steps;
    per_track = (size_t)n_steps * 4;
    out_doubles = (size_t)n_tracks * per_track;
    kf_header_params(&r.hdr, &p);

    if ((uint64_t)workers > n_tracks) {
        LOG_ERROR("%d workers for %llu tracks: parallelism is across tracks, so at most one worker per track",
                  workers, (unsigned long long)n_tracks);
        kf_reader_close(&r);
        return 2;
    }
    if (kf_sibling_path("kf_worker", exe, sizeof exe) != 0) {
        kf_reader_close(&r);
        return 1;
    }

    slot = (worker_slot *)calloc((size_t)workers, sizeof *slot);
    round_ms = (double *)malloc((size_t)repeat * sizeof *round_ms);
    cpu_round = (double *)calloc((size_t)repeat, sizeof *cpu_round);
    if (slot == NULL || round_ms == NULL || cpu_round == NULL) {
        LOG_ERROR("out of memory");
        goto cleanup;
    }
    for (i = 0; i < workers; i++) {
        slot[i].cmd_w = PLAT_INVALID_HANDLE;
        slot[i].res_r = PLAT_INVALID_HANDLE;
    }

    /* ---- 1+2: shared output segment, pipes, workers ----------------------- */
    snprintf(shm_name, sizeof shm_name, "sk_kf_%lu", plat_getpid());
    if (plat_shm_create(shm_name, out_doubles * sizeof(double), &shm) != 0) {
        goto cleanup;
    }
    shm_made = 1;

    for (i = 0; i < workers; i++) {
        plat_handle cmd_r;
        plat_handle res_w;
        plat_handle keep[2];
        char s_id[16];
        char s_cmd[32];
        char s_res[32];
        char *argv_w[16];
        int n = 0;

        if (plat_pipe(&cmd_r, &slot[i].cmd_w) != 0) {
            goto cleanup;
        }
        if (plat_pipe(&slot[i].res_r, &res_w) != 0) {
            close_handle(&cmd_r);
            goto cleanup;
        }
        snprintf(s_id, sizeof s_id, "%d", i);
        snprintf(s_cmd, sizeof s_cmd, "%lld", (long long)cmd_r);
        snprintf(s_res, sizeof s_res, "%lld", (long long)res_w);
        argv_w[n++] = (char *)"kf_worker";
        argv_w[n++] = (char *)"--id";
        argv_w[n++] = s_id;
        argv_w[n++] = (char *)"--in";
        argv_w[n++] = (char *)in;
        argv_w[n++] = (char *)"--shm";
        argv_w[n++] = shm_name;
        argv_w[n++] = (char *)"--cmd";
        argv_w[n++] = s_cmd;
        argv_w[n++] = (char *)"--res";
        argv_w[n++] = s_res;
        if (inject != NULL && i == workers - 1) {
            /* failure tests: the last worker is told to fail */
            argv_w[n++] = (char *)"--fail";
            argv_w[n++] = (char *)inject;
        }
        argv_w[n] = NULL;
        keep[0] = cmd_r;
        keep[1] = res_w;
        if (plat_spawn(exe, argv_w, keep, 2, &slot[i].proc) != 0) {
            close_handle(&cmd_r);
            close_handle(&res_w);
            goto cleanup;
        }
        slot[i].started = 1;
        spawned++;
        /* The child has its own copies; closing ours means that when the child
         * dies its pipe ends close and we see end-of-file instead of waiting forever. */
        close_handle(&cmd_r);
        close_handle(&res_w);
    }

    for (i = 0; i < workers; i++) {
        kf_msg m;

        if (expect(&slot[i], i, KF_MSG_READY, &m) != 0) {
            failed = 1;
            goto cleanup;
        }
    }
    t_up = plat_now_ns();

    /* ---- 3: the timed rounds ---------------------------------------------- */
    for (r_ok = 0; r_ok < repeat; r_ok++) {
        uint64_t a0 = plat_now_ns();
        uint64_t max_ns = 0;
        uint64_t sum_ns = 0;
        double cpu_us = 0.0;

        for (i = 0; i < workers; i++) {
            kf_msg m;
            uint64_t first;
            uint64_t count;

            kf_split_tracks(n_tracks, workers, i, &first, &count);
            memset(&m, 0, sizeof m);
            m.type = KF_MSG_GO;
            m.worker = (uint32_t)i;
            m.a = first;
            m.b = count;
            m.c = (uint64_t)r_ok;
            if (kf_msg_send(slot[i].cmd_w, &m) != 0) {
                LOG_ERROR("cannot send GO to worker %d (it died)", i);
                failed = 1;
                goto cleanup;
            }
        }
        for (i = 0; i < workers; i++) {
            kf_msg m;

            if (expect(&slot[i], i, KF_MSG_DONE, &m) != 0) {
                failed = 1;
                goto cleanup;
            }
            if (m.a > max_ns) {
                max_ns = m.a;
            }
            sum_ns += m.a;
            cpu_us += (double)(m.b + m.c);
            if (m.d > worker_peak_kb) {
                worker_peak_kb = m.d;
            }
        }
        round_ms[r_ok] = ms(plat_now_ns() - a0);
        cpu_round[r_ok] = cpu_us / 1e3;
        imbalance_sum += (double)max_ns / ((double)sum_ns / workers);
    }

    /* ---- 4: stop the workers ---------------------------------------------- */
    for (i = 0; i < workers; i++) {
        kf_msg m;

        memset(&m, 0, sizeof m);
        m.type = KF_MSG_STOP;
        m.worker = (uint32_t)i;
        if (kf_msg_send(slot[i].cmd_w, &m) != 0) {
            LOG_ERROR("cannot send STOP to worker %d", i);
            failed = 1;
            goto cleanup;
        }
    }
    for (i = 0; i < workers; i++) {
        int code = -1;

        close_handle(&slot[i].cmd_w);
        close_handle(&slot[i].res_r);
        if (plat_wait(slot[i].proc, &code) != 0 || code != 0) {
            LOG_ERROR("worker %d exited with code %d", i, code);
            failed = 1;
        }
        slot[i].started = 0;
        spawned--;
    }
    if (failed) {
        goto cleanup;
    }

    /* ---- 5: hash, verify, write ------------------------------------------- */
    t_hash0 = plat_now_ns();
    hash = kf_hash_update(KF_HASH_INIT, shm.addr, out_doubles * sizeof(double));
    t_hash = plat_now_ns() - t_hash0;

    st.verified = "n/a";
    if (expect_file != NULL) {
        kf_reader ref;

        if (kf_reader_open(&ref, expect_file, KF_MAGIC_EST, 1) != 0) {
            goto cleanup;
        }
        if (ref.hdr.f.n_tracks != n_tracks || ref.hdr.f.n_steps != n_steps) {
            LOG_ERROR("%s has a different shape than the input", expect_file);
            kf_reader_close(&ref);
            goto cleanup;
        }
        st.verified = (ref.hdr.f.data_hash == hash) ? "yes" : "no";
        if (ref.hdr.f.data_hash != hash) {
            LOG_ERROR("RESULT MISMATCH: parallel hash 0x%016llx, serial hash 0x%016llx",
                      (unsigned long long)hash, (unsigned long long)ref.hdr.f.data_hash);
            kf_reader_close(&ref);
            failed = 1;
            goto cleanup;
        }
        kf_reader_close(&ref);
    }

    if (out != NULL) {
        kf_file_header eh;
        kf_writer w;
        uint64_t a0 = plat_now_ns();
        uint64_t t;

        kf_header_init(&eh, KF_MAGIC_EST, 4, n_tracks, n_steps, &p);
        if (kf_writer_open(&w, out, &eh) != 0) {
            goto cleanup;
        }
        for (t = 0; t < n_tracks; t++) {
            if (kf_writer_append_nohash(&w, (const double *)shm.addr + t * per_track, per_track) != 0) {
                goto cleanup;
            }
        }
        kf_writer_set_hash(&w, hash);
        if (kf_writer_close(&w) != 0) {
            goto cleanup;
        }
        t_write = plat_now_ns() - a0;
    }

    (void)plat_usage_self(&mu);
    {
        double cpu = 0.0;
        double steps_total = (double)n_tracks * (double)n_steps;

        for (i = 0; i < repeat; i++) {
            cpu += cpu_round[i];
        }
        st.program = "master";
        st.workers = workers;
        st.io = "mmap";
        st.tracks = n_tracks;
        st.steps = n_steps;
        st.startup_ms = ms(t_up - t_loaded);
        st.load_ms = ms(t_loaded - t_start);
        st.compute_first_ms = round_ms[0];
        st.rounds = repeat;
        st.compute_ms = kf_median(round_ms, repeat);
        st.hash_ms = ms(t_hash);
        st.write_ms = ms(t_write);
        st.total_ms = ms(plat_now_ns() - t_start);
        st.cpu_ms = cpu / repeat;
        st.peak_kb = worker_peak_kb;
        st.master_peak_kb = mu.peak_kb;
        st.hash = hash;

        printf("kf_master: %llu tracks x %llu steps, %d worker%s, %d round%s\n", (unsigned long long)n_tracks,
               (unsigned long long)n_steps, workers, workers == 1 ? "" : "s", repeat, repeat == 1 ? "" : "s");
        printf("  load    : %9.2f ms\n", st.load_ms);
        printf("  startup : %9.2f ms  (shared memory, pipes, %d process%s)\n", st.startup_ms, workers,
               workers == 1 ? "" : "es");
        printf("  compute : %9.2f ms  (%.2f million steps/s)%s\n", st.compute_ms,
               steps_total / st.compute_ms / 1e3, repeat > 1 ? "  median of rounds" : "");
        if (repeat > 1) {
            printf("  1st rnd : %9.2f ms\n", st.compute_first_ms);
        }
        printf("  hash    : %9.2f ms\n", st.hash_ms);
        printf("  write   : %9.2f ms%s\n", st.write_ms, out != NULL ? "" : "  (skipped)");
        printf("  total   : %9.2f ms\n", st.total_ms);
        printf("  balance : slowest worker %.3fx the average\n", imbalance_sum / repeat);
        printf("  verified: %s\n", st.verified);
        printf("est_hash=0x%016llx\n", (unsigned long long)hash);
        if (csv != NULL && kf_stats_append_csv(csv, &st) != 0) {
            goto cleanup;
        }
    }
    rc = 0;

cleanup:
    /* Closing our command pipes makes any worker still waiting for a command
     * see end-of-file and exit; then every started worker is waited for. */
    if (slot != NULL) {
        for (i = 0; i < workers; i++) {
            close_handle(&slot[i].cmd_w);
            close_handle(&slot[i].res_r);
        }
        for (i = 0; i < workers; i++) {
            if (slot[i].started) {
                int code = 0;

                (void)plat_wait(slot[i].proc, &code);
            }
        }
    }
    if (shm_made) {
        (void)plat_shm_destroy(&shm);
    }
    kf_reader_close(&r);
    free(slot);
    free(round_ms);
    free(cpu_round);
    if (failed) {
        fprintf(stderr, "kf_master: FAILED\n");
        return 1;
    }
    return rc;
}
