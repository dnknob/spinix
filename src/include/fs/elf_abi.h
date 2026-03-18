#ifndef ELF_ABI_H
#define ELF_ABI_H

#include <klibc/types.h>

#include <core/proc.h>

#define ELF_OK              0
#define ELF_ERR_BADMAGIC   -1   /* not an ELF file              */
#define ELF_ERR_BADCLASS   -2   /* not 64-bit                   */
#define ELF_ERR_BADARCH    -3   /* not x86-64                   */
#define ELF_ERR_BADTYPE    -4   /* not ET_EXEC or ET_DYN        */
#define ELF_ERR_BADPHOFF   -5   /* program headers out of range */
#define ELF_ERR_NOMEM      -6   /* allocation failure           */
#define ELF_ERR_MMAP       -7   /* failed to map a segment      */

pcb_t *elf_load(const void *data, size_t size,
                const char *name, uint8_t priority,
                int *err_out);

pcb_t *elf_load_from_path(const char *path, const char *name,
                          uint8_t priority, int *err_out);

const char *elf_strerror(int err);

#endif