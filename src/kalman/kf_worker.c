/*
 * kf_worker - one worker process of the parallel filter. Started by kf_master
 * (plat_spawn); not meant to be run by hand.
 *
 *   kf_worker --id N --in FILE --shm NAME --cmd HANDLE --res HANDLE [--fail start|compute]
 *
 * The worker maps the measurement file (read-only, so every worker shares the
 * same page-cache pages), attaches the master's shared-memory output segment,
 * and then serves GO messages: filter the assigned block of tracks, write the
 * estimates straight into their place in the shared segment, report the time.
 * Nothing but the 40-byte control messages travels through the pipes.
 *
 * --fail is for the failure tests: "start" exits before reporting READY,
 * "compute" exits halfway through the first GO.
 */

#include "kalman.h"
#include "kfio.h"
#include "kfproto.h"
#include "log.h"
#include "platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { ERR_NONE = 0, ERR_INPUT = 1, ERR_SHM = 2, ERR_RANGE = 3 };

static void send_error(plat_handle res, int worker, uint64_t code)
{
    kf_msg m;

    memset(&m, 0, sizeof m);
    m.type = KF_MSG_ERROR;
    m.worker = (uint32_t)worker;
    m.a = code;
    (void)kf_msg_send(res, &m);
}

int main(int argc, char **argv)
{
    const char *in = NULL;
    const char *shm_name = NULL;
    const char *fail = NULL;
    plat_handle cmd = PLAT_INVALID_HANDLE;
    plat_handle res = PLAT_INVALID_HANDLE;
    int id = -1;
    kf_reader r;
    kf_model model;
    kf_gen_params p;
    plat_shm shm;
    uint64_t n_tracks;
    uint64_t n_steps;
    size_t per_track;
    int rc = 1;
    int i;

    log_init(NULL);
    for (i = 1; i + 1 < argc; i += 2) {
        const char *a = argv[i];
        const char *v = argv[i + 1];

        if (strcmp(a, "--id") == 0) {
            id = atoi(v);
        } else if (strcmp(a, "--in") == 0) {
            in = v;
        } else if (strcmp(a, "--shm") == 0) {
            shm_name = v;
        } else if (strcmp(a, "--cmd") == 0) {
            cmd = (plat_handle)strtoll(v, NULL, 10);
        } else if (strcmp(a, "--res") == 0) {
            res = (plat_handle)strtoll(v, NULL, 10);
        } else if (strcmp(a, "--fail") == 0) {
            fail = v;
        } else {
            fprintf(stderr, "kf_worker: unknown option %s\n", a);
            return 2;
        }
    }
    if (id < 0 || in == NULL || shm_name == NULL || cmd == PLAT_INVALID_HANDLE || res == PLAT_INVALID_HANDLE) {
        fprintf(stderr, "kf_worker: started by kf_master; see the source for the options\n");
        return 2;
    }

    memset(&shm, 0, sizeof shm);
    if (kf_reader_open(&r, in, KF_MAGIC_MEAS, 1) != 0) {
        send_error(res, id, ERR_INPUT);
        return 1;
    }
    n_tracks = r.hdr.f.n_tracks;
    n_steps = r.hdr.f.n_steps;
    per_track = (size_t)n_steps * 4;
    kf_header_params(&r.hdr, &p);
    kf_model_init(&model, p.dt, p.sigma_a, p.sigma_r, p.sigma_p0, p.sigma_v0);

    if (plat_shm_attach(shm_name, (size_t)(n_tracks * per_track * sizeof(double)), &shm) != 0) {
        send_error(res, id, ERR_SHM);
        goto done;
    }

    if (fail != NULL && strcmp(fail, "start") == 0) {
        LOG_WARN("worker %d: injected failure at start", id);
        goto done; /* exits non-zero without ever sending READY */
    }

    {
        kf_msg m;

        memset(&m, 0, sizeof m);
        m.type = KF_MSG_READY;
        m.worker = (uint32_t)id;
        m.a = plat_getpid();
        if (kf_msg_send(res, &m) != 0) {
            goto done;
        }
    }

    for (;;) {
        kf_msg m;
        int got = kf_msg_recv(cmd, &m);

        if (got == 0) {
            LOG_WARN("worker %d: master closed the command pipe without STOP", id);
            rc = 3;
            goto done;
        }
        if (got < 0) {
            goto done;
        }
        if (m.type == KF_MSG_STOP) {
            rc = 0;
            goto done;
        }
        if (m.type == KF_MSG_GO) {
            uint64_t first = m.a;
            uint64_t count = m.b;
            uint64_t t;
            plat_usage u0;
            plat_usage u1;
            uint64_t t0;
            uint64_t t1;
            kf_msg d;

            if (first > n_tracks || count > n_tracks - first) {
                send_error(res, id, ERR_RANGE);
                goto done;
            }
            (void)plat_usage_self(&u0);
            t0 = plat_now_ns();
            for (t = first; t < first + count; t++) {
                kf_run_track(&model, r.data + t * n_steps * 2, (size_t)n_steps,
                             (double *)shm.addr + t * per_track);
                if (fail != NULL && strcmp(fail, "compute") == 0) {
                    LOG_WARN("worker %d: injected failure during compute", id);
                    goto done;
                }
            }
            t1 = plat_now_ns();
            (void)plat_usage_self(&u1);

            memset(&d, 0, sizeof d);
            d.type = KF_MSG_DONE;
            d.worker = (uint32_t)id;
            d.a = t1 - t0;
            d.b = u1.user_us - u0.user_us;
            d.c = u1.sys_us - u0.sys_us;
            d.d = u1.peak_kb;
            if (kf_msg_send(res, &d) != 0) {
                goto done;
            }
            continue;
        }
        LOG_ERROR("worker %d: unexpected message type %u", id, (unsigned)m.type);
        goto done;
    }

done:
    if (shm.addr != NULL) {
        (void)plat_shm_detach(&shm);
    }
    kf_reader_close(&r);
    (void)plat_close(cmd);
    (void)plat_close(res);
    return rc;
}
