#ifndef ELF_H
#define ELF_H

#include <klibc/types.h>

#define ELF_MAGIC0      0x7F
#define ELF_MAGIC1      'E'
#define ELF_MAGIC2      'L'
#define ELF_MAGIC3      'F'

#define EI_MAG0         0
#define EI_MAG1         1
#define EI_MAG2         2
#define EI_MAG3         3
#define EI_CLASS        4
#define EI_DATA         5
#define EI_VERSION      6
#define EI_OSABI        7
#define EI_NIDENT       16

#define ELFCLASS32      1
#define ELFCLASS64      2

#define ELFDATA2LSB     1   /* little-endian */

#define ET_NONE         0
#define ET_REL          1
#define ET_EXEC         2   /* static executable   */
#define ET_DYN          3   /* PIE / shared object */

#define EM_X86_64       62

#define EV_CURRENT      1

#define PT_NULL         0
#define PT_LOAD         1
#define PT_DYNAMIC      2
#define PT_INTERP       3
#define PT_NOTE         4
#define PT_PHDR         6
#define PT_GNU_STACK    0x6474e551
#define PT_GNU_RELRO    0x6474e552

#define PF_X            (1 << 0)    /* Execute */
#define PF_W            (1 << 1)    /* Write   */
#define PF_R            (1 << 2)    /* Read    */

typedef uint64_t Elf64_Addr;
typedef uint64_t Elf64_Off;
typedef uint16_t Elf64_Half;
typedef uint32_t Elf64_Word;
typedef int32_t  Elf64_Sword;
typedef uint64_t Elf64_Xword;
typedef int64_t  Elf64_Sxword;

typedef struct {
    uint8_t     e_ident[EI_NIDENT];
    Elf64_Half  e_type;
    Elf64_Half  e_machine;
    Elf64_Word  e_version;
    Elf64_Addr  e_entry;        /* virtual entry point          */
    Elf64_Off   e_phoff;        /* program header table offset  */
    Elf64_Off   e_shoff;        /* section header table offset  */
    Elf64_Word  e_flags;
    Elf64_Half  e_ehsize;       /* ELF header size (64)         */
    Elf64_Half  e_phentsize;    /* program header entry size    */
    Elf64_Half  e_phnum;        /* number of program headers    */
    Elf64_Half  e_shentsize;
    Elf64_Half  e_shnum;
    Elf64_Half  e_shstrndx;
} __attribute__((packed)) Elf64_Ehdr;

typedef struct {
    Elf64_Word  p_type;
    Elf64_Word  p_flags;
    Elf64_Off   p_offset;       /* offset in file               */
    Elf64_Addr  p_vaddr;        /* virtual address in memory    */
    Elf64_Addr  p_paddr;        /* physical (ignored)           */
    Elf64_Xword p_filesz;       /* bytes in file                */
    Elf64_Xword p_memsz;        /* bytes in memory (>= filesz)  */
    Elf64_Xword p_align;        /* alignment (power of 2)       */
} __attribute__((packed)) Elf64_Phdr;

#endif