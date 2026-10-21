/*
 * kfio.h - the binary file format shared by the generator, the serial filter,
 * the parallel master and the Python reference, and the code that reads and
 * writes it. All file access goes through the plat_* wrappers (open, read,
 * write, close, mmap), never stdio.
 *
 * Layout (little-endian, x86-64):
 *   bytes 0 .. 4095        header (kf_file_header, padded to one 4 KB page)
 *   bytes 4096 ..          data: n_tracks blocks, each n_steps rows of
 *                          values_per_step doubles ("track-major")
 *
 *   measurement file  magic "SKKFMEAS"  2 doubles per step: zx, zy
 *   estimate file     magic "SKKFEST0"  4 doubles per step: px, py, vx, vy
 *
 * The header holds the model parameters, so a file is self-describing, and a
 * hash of the data region so two files can be compared without reading both.
 * The data starts on a page boundary, so a memory-mapped file can be used in
 * place, and each track is contiguous, so a worker can take a block of tracks.
 */
#ifndef KFIO_H
#define KFIO_H

#include "datagen.h"
#include "platform.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KF_HEADER_BYTES 4096
#define KF_MAGIC_MEAS "SKKFMEAS"
#define KF_MAGIC_EST  "SKKFEST0"

typedef union {
    struct {
        char     magic[8];
        uint32_t version;          /* 1 */
        uint32_t values_per_step;  /* 2 (measurements) or 4 (estimates) */
        uint64_t n_tracks;
        uint64_t n_steps;
        uint64_t seed;
        double   dt;
        double   sigma_a;
        double   sigma_r;
        double   sigma_p0;
        double   sigma_v0;
        uint64_t data_hash;        /* kf_hash over the data region */
    } f;
    unsigned char raw[KF_HEADER_BYTES];
} kf_file_header;

/* ---- hash -----------------------------------------------------------------
 * FNV-1a, applied to 64-bit words instead of bytes (8x faster, same idea):
 *   h = (h ^ word) * 1099511628211, starting from 14695981039346656037.
 * `bytes` must be a multiple of 8. The result depends only on the data. */
#define KF_HASH_INIT 14695981039346656037ull
uint64_t kf_hash_update(uint64_t h, const void *data, size_t bytes);

/* ---- header helpers ------------------------------------------------------- */
void kf_header_init(kf_file_header *h, const char magic[8], uint32_t values_per_step,
                    uint64_t n_tracks, uint64_t n_steps, const kf_gen_params *p);
void kf_header_params(const kf_file_header *h, kf_gen_params *p);
size_t kf_header_data_bytes(const kf_file_header *h);

/* ---- writing ---------------------------------------------------------------
 * kf_writer_open writes a header with hash 0; kf_writer_append adds data and
 * hashes it on the way; kf_writer_close checks that exactly the promised amount
 * was written and patches the final hash into the header (through a writable
 * file mapping). Returns 0 / -1; failures are logged. */
typedef struct {
    plat_handle    h;
    char           path[512];
    kf_file_header hdr;
    uint64_t       hash;
    uint64_t       bytes;
} kf_writer;

int kf_writer_open(kf_writer *w, const char *path, const kf_file_header *hdr);
int kf_writer_append(kf_writer *w, const double *data, size_t n_doubles);
/* Same, but does not hash: for callers that hashed the data themselves (the
 * parallel master hashes the shared output once). Give the hash before close. */
int kf_writer_append_nohash(kf_writer *w, const double *data, size_t n_doubles);
void kf_writer_set_hash(kf_writer *w, uint64_t hash);
int kf_writer_close(kf_writer *w);

/* ---- reading ---------------------------------------------------------------
 * use_mmap = 1: map the file and use it in place (pages arrive on first touch).
 * use_mmap = 0: read the whole file into allocated memory with plat_read.
 * Either way `data` points at the first value; the memory stays valid until
 * kf_reader_close. */
typedef struct {
    kf_file_header hdr;
    const double  *data;
    size_t         n_doubles;
    int            mmapped;
    plat_handle    h;
    void          *base;       /* mapping, or allocation */
    size_t         base_len;
} kf_reader;

int  kf_reader_open(kf_reader *r, const char *path, const char magic[8], int use_mmap);
uint64_t kf_reader_hash(const kf_reader *r);   /* hash of the data actually present */
void kf_reader_close(kf_reader *r);

#ifdef __cplusplus
}
#endif

#endif /* KFIO_H */
