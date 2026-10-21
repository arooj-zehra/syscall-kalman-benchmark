#include "kfio.h"

#include "log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

_Static_assert(sizeof(kf_file_header) == KF_HEADER_BYTES, "header must be exactly one page");

uint64_t kf_hash_update(uint64_t h, const void *data, size_t bytes)
{
    const unsigned char *p = (const unsigned char *)data;
    size_t i;

    for (i = 0; i + 8 <= bytes; i += 8) {
        uint64_t w;

        memcpy(&w, p + i, 8);
        h = (h ^ w) * 1099511628211ull;
    }
    return h;
}

void kf_header_init(kf_file_header *h, const char magic[8], uint32_t values_per_step,
                    uint64_t n_tracks, uint64_t n_steps, const kf_gen_params *p)
{
    memset(h, 0, sizeof *h);
    memcpy(h->f.magic, magic, 8);
    h->f.version = 1;
    h->f.values_per_step = values_per_step;
    h->f.n_tracks = n_tracks;
    h->f.n_steps = n_steps;
    h->f.seed = p->seed;
    h->f.dt = p->dt;
    h->f.sigma_a = p->sigma_a;
    h->f.sigma_r = p->sigma_r;
    h->f.sigma_p0 = p->sigma_p0;
    h->f.sigma_v0 = p->sigma_v0;
}

void kf_header_params(const kf_file_header *h, kf_gen_params *p)
{
    p->seed = h->f.seed;
    p->dt = h->f.dt;
    p->sigma_a = h->f.sigma_a;
    p->sigma_r = h->f.sigma_r;
    p->sigma_p0 = h->f.sigma_p0;
    p->sigma_v0 = h->f.sigma_v0;
}

size_t kf_header_data_bytes(const kf_file_header *h)
{
    return (size_t)h->f.n_tracks * (size_t)h->f.n_steps * h->f.values_per_step * sizeof(double);
}

/* ---- writing --------------------------------------------------------------- */

static int write_all(plat_handle h, const void *buf, size_t len)
{
    const unsigned char *p = (const unsigned char *)buf;

    while (len > 0) {
        size_t chunk = len < (size_t)(32u << 20) ? len : (size_t)(32u << 20);
        int64_t n = plat_write(h, p, chunk);

        if (n <= 0) {
            return -1;
        }
        p += n;
        len -= (size_t)n;
    }
    return 0;
}

int kf_writer_open(kf_writer *w, const char *path, const kf_file_header *hdr)
{
    memset(w, 0, sizeof *w);
    snprintf(w->path, sizeof w->path, "%s", path);
    w->hdr = *hdr;
    w->hdr.f.data_hash = 0;
    w->hash = KF_HASH_INIT;
    w->h = plat_open(path, PLAT_OPEN_WRITE | PLAT_OPEN_CREATE | PLAT_OPEN_TRUNC, 0644);
    if (w->h == PLAT_INVALID_HANDLE) {
        return -1;
    }
    if (write_all(w->h, &w->hdr, sizeof w->hdr) != 0) {
        (void)plat_close(w->h);
        w->h = PLAT_INVALID_HANDLE;
        return -1;
    }
    return 0;
}

int kf_writer_append(kf_writer *w, const double *data, size_t n_doubles)
{
    if (write_all(w->h, data, n_doubles * sizeof(double)) != 0) {
        LOG_ERROR("write to %s failed", w->path);
        return -1;
    }
    w->hash = kf_hash_update(w->hash, data, n_doubles * sizeof(double));
    w->bytes += (uint64_t)n_doubles * sizeof(double);
    return 0;
}

int kf_writer_append_nohash(kf_writer *w, const double *data, size_t n_doubles)
{
    if (write_all(w->h, data, n_doubles * sizeof(double)) != 0) {
        LOG_ERROR("write to %s failed", w->path);
        return -1;
    }
    w->bytes += (uint64_t)n_doubles * sizeof(double);
    return 0;
}

void kf_writer_set_hash(kf_writer *w, uint64_t hash)
{
    w->hash = hash;
}

int kf_writer_close(kf_writer *w)
{
    plat_handle h;
    void *map;
    int rc = 0;

    if (plat_close(w->h) != 0) {
        rc = -1;
    }
    w->h = PLAT_INVALID_HANDLE;
    if (w->bytes != kf_header_data_bytes(&w->hdr)) {
        LOG_ERROR("%s: wrote %llu data bytes, header promises %llu", w->path,
                  (unsigned long long)w->bytes, (unsigned long long)kf_header_data_bytes(&w->hdr));
        return -1;
    }
    w->hdr.f.data_hash = w->hash;

    /* Patch the hash into the header through a writable mapping of the first page. */
    h = plat_open(w->path, PLAT_OPEN_READ | PLAT_OPEN_WRITE, 0);
    if (h == PLAT_INVALID_HANDLE) {
        return -1;
    }
    map = plat_map_file(h, KF_HEADER_BYTES, 1);
    if (map == NULL) {
        (void)plat_close(h);
        return -1;
    }
    memcpy(map, &w->hdr, sizeof w->hdr);
    if (plat_unmap_file(map, KF_HEADER_BYTES) != 0) {
        rc = -1;
    }
    if (plat_close(h) != 0) {
        rc = -1;
    }
    return rc;
}

/* ---- reading ---------------------------------------------------------------- */

static int check_header(const kf_file_header *h, const char magic[8], size_t file_size, const char *path)
{
    if (memcmp(h->f.magic, magic, 8) != 0) {
        LOG_ERROR("%s: not a %.8s file (bad magic)", path, magic);
        return -1;
    }
    if (h->f.version != 1) {
        LOG_ERROR("%s: unsupported version %u", path, (unsigned)h->f.version);
        return -1;
    }
    if (h->f.values_per_step != 2 && h->f.values_per_step != 4) {
        LOG_ERROR("%s: bad values_per_step %u", path, (unsigned)h->f.values_per_step);
        return -1;
    }
    if ((uint64_t)file_size != KF_HEADER_BYTES + kf_header_data_bytes(h)) {
        LOG_ERROR("%s: size %llu does not match the header (expected %llu)", path,
                  (unsigned long long)file_size,
                  (unsigned long long)(KF_HEADER_BYTES + kf_header_data_bytes(h)));
        return -1;
    }
    return 0;
}

int kf_reader_open(kf_reader *r, const char *path, const char magic[8], int use_mmap)
{
    plat_stat st;
    size_t size;

    memset(r, 0, sizeof *r);
    r->h = PLAT_INVALID_HANDLE;
    if (plat_stat_path(path, &st) != 0) {
        return -1;
    }
    size = (size_t)st.size;
    if (size < KF_HEADER_BYTES) {
        LOG_ERROR("%s: too small to be a data file (%llu bytes)", path, (unsigned long long)size);
        return -1;
    }
    r->h = plat_open(path, PLAT_OPEN_READ, 0);
    if (r->h == PLAT_INVALID_HANDLE) {
        return -1;
    }

    if (use_mmap) {
        r->base = plat_map_file(r->h, size, 0);
        if (r->base == NULL) {
            goto fail;
        }
        r->mmapped = 1;
    } else {
        unsigned char *buf = (unsigned char *)malloc(size);
        size_t got = 0;

        if (buf == NULL) {
            LOG_ERROR("out of memory reading %s (%llu bytes)", path, (unsigned long long)size);
            goto fail;
        }
        while (got < size) {
            size_t chunk = size - got < (size_t)(32u << 20) ? size - got : (size_t)(32u << 20);
            int64_t n = plat_read(r->h, buf + got, chunk);

            if (n <= 0) {
                free(buf);
                goto fail;
            }
            got += (size_t)n;
        }
        r->base = buf;
        /* The file handle is no longer needed once everything is in memory. */
        (void)plat_close(r->h);
        r->h = PLAT_INVALID_HANDLE;
    }
    r->base_len = size;

    memcpy(&r->hdr, r->base, sizeof r->hdr);
    if (check_header(&r->hdr, magic, size, path) != 0) {
        kf_reader_close(r);
        return -1;
    }
    r->data = (const double *)((const unsigned char *)r->base + KF_HEADER_BYTES);
    r->n_doubles = (size - KF_HEADER_BYTES) / sizeof(double);
    return 0;

fail:
    if (r->h != PLAT_INVALID_HANDLE) {
        (void)plat_close(r->h);
        r->h = PLAT_INVALID_HANDLE;
    }
    return -1;
}

uint64_t kf_reader_hash(const kf_reader *r)
{
    return kf_hash_update(KF_HASH_INIT, r->data, r->n_doubles * sizeof(double));
}

void kf_reader_close(kf_reader *r)
{
    if (r->base != NULL) {
        if (r->mmapped) {
            (void)plat_unmap_file(r->base, r->base_len);
        } else {
            free(r->base);
        }
        r->base = NULL;
    }
    if (r->h != PLAT_INVALID_HANDLE) {
        (void)plat_close(r->h);
        r->h = PLAT_INVALID_HANDLE;
    }
    r->data = NULL;
}
