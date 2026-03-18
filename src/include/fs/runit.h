#ifndef RUNIT_H
#define RUNIT_H

#include <klibc/types.h>

#include <core/proc.h>
#include <core/scheduler.h>

pcb_t *runit(const char *path, const char *name,
             uint8_t priority, int *err_out);

pcb_t *runit_buf(const void *data, size_t size, const char *name,
                 uint8_t priority, int *err_out);

typedef struct {
    const uint8_t *start;
    const uint8_t *end;
    const char    *name;
} runit_embed_t;

#define RUNIT_EMBED(sym, vfs_path)                          \
    extern const uint8_t _binary_bin_##sym##_start[];       \
    extern const uint8_t _binary_bin_##sym##_end[];         \
    static const runit_embed_t runit_##sym = {              \
        .start = _binary_bin_##sym##_start,                 \
        .end   = _binary_bin_##sym##_end,                   \
        .name  = (vfs_path),                                \
    }

pcb_t *runit_embedded(const runit_embed_t *emb, const char *name,
                      uint8_t priority, int *err_out);

int runit_wait(pcb_t *proc);
int runit_done(pcb_t *proc);

int runit_install(const char *vfs_path,
                  const uint8_t *data, size_t size);

#endif