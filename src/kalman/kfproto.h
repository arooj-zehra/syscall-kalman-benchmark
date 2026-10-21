/*
 * kfproto.h - what the serial program, the master and the workers share:
 * the control messages that travel through pipes, how work is divided, how a
 * program finds its sibling executables, and the timing record every program
 * writes (so serial and parallel rows go into one table).
 */
#ifndef KFPROTO_H
#define KFPROTO_H

#include "platform.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- control messages ------------------------------------------------------
 * Fixed 40 bytes, sent through the pipes between master and a worker:
 *
 *   worker -> master  READY   a = worker pid
 *   master -> worker  GO      a = first track, b = number of tracks, c = round
 *   worker -> master  DONE    a = compute time (ns), b = CPU user us, c = CPU system us,
 *                             d = peak resident memory (KB); CPU and time are for this round
 *   master -> worker  STOP    (the worker detaches and exits 0)
 *   worker -> master  ERROR   a = what failed (a short code), then the worker exits
 */
enum { KF_MSG_READY = 1, KF_MSG_GO = 2, KF_MSG_STOP = 3, KF_MSG_DONE = 4, KF_MSG_ERROR = 5 };

typedef struct {
    uint32_t type;
    uint32_t worker;
    uint64_t a;
    uint64_t b;
    uint64_t c;
    uint64_t d;
} kf_msg;

/* 0 on success, -1 on failure (including a closed peer). */
int kf_msg_send(plat_handle h, const kf_msg *m);
/* 1 = message received, 0 = the peer closed the pipe (end of data between
 * messages), -1 = error or a message cut short. */
int kf_msg_recv(plat_handle h, kf_msg *m);

/* ---- work division ---------------------------------------------------------
 * Contiguous blocks of whole tracks: with T tracks and W workers every worker
 * gets T / W tracks and the first T % W workers get one more. */
void kf_split_tracks(uint64_t tracks, int workers, int index, uint64_t *first, uint64_t *count);

/* Full path of an executable that sits next to the running one, e.g.
 * kf_sibling_path("kf_worker", ...) -> ".../build/bin/kf_worker(.exe)". */
int kf_sibling_path(const char *name, char *out, size_t n);

/* ---- timing record ---------------------------------------------------------- */
typedef struct {
    const char *program;      /* "serial" or "master" */
    int         workers;      /* 1 for serial */
    const char *io;           /* how the input was read: mmap or read */
    uint64_t    tracks;
    uint64_t    steps;
    double      startup_ms;   /* parallel: creating shm and pipes, starting workers, until all ready */
    double      load_ms;      /* opening / mapping / reading the input */
    double      compute_ms;   /* median over rounds of the time to filter every track */
    double      compute_first_ms; /* the first round: includes the first touch of every page */
    double      hash_ms;      /* hashing the result */
    double      write_ms;     /* writing the result file (0 if skipped) */
    double      total_ms;     /* the whole run */
    int         rounds;
    double      cpu_ms;       /* CPU time (user + system) of all processes, per round */
    uint64_t    peak_kb;      /* largest peak resident memory of one filtering process */
    uint64_t    master_peak_kb; /* the master's own peak (0 for serial) */
    uint64_t    hash;         /* hash of the result */
    const char *verified;     /* "ref" (serial), "yes", "no" or "n/a" (parallel run without a reference) */
} kf_run_stats;

/* Append one row to a CSV file, writing the header first if the file is new. */
int kf_stats_append_csv(const char *path, const kf_run_stats *s);

/* Median of v[0..n-1] (sorts the array). */
double kf_median(double *v, int n);

#ifdef __cplusplus
}
#endif

#endif /* KFPROTO_H */
