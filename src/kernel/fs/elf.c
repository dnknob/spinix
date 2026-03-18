#include <fs/elf_abi.h>
#include <fs/elf.h>
#include <fs/vfs.h>

#include <mm/vmm.h>
#include <mm/pmm.h>
#include <mm/heap.h>
#include <mm/mmu.h>

#include <core/proc.h>
#include <core/scheduler.h>

#include <arch/x86_64/mmu.h>

#include <video/printk.h>

#include <klibc/string.h>

#define USER_STACK_PAGES    16
#define USER_STACK_TOP      0x00007FFFFFFFE000ULL
#define USER_STACK_BOTTOM   (USER_STACK_TOP - (USER_STACK_PAGES * PAGE_SIZE))

static uint32_t phdr_to_vmm_flags(Elf64_Word pf)
{
    uint32_t f = VMM_USER;
    if (pf & PF_R) f |= VMM_READ;
    if (pf & PF_W) f |= VMM_WRITE;
    if (pf & PF_X) f |= VMM_EXEC;
    return f;
}

const char *elf_strerror(int err)
{
    switch (err) {
    case ELF_OK:            return "success";
    case ELF_ERR_BADMAGIC:  return "bad ELF magic";
    case ELF_ERR_BADCLASS:  return "not a 64-bit ELF";
    case ELF_ERR_BADARCH:   return "not x86-64";
    case ELF_ERR_BADTYPE:   return "not an executable";
    case ELF_ERR_BADPHOFF:  return "program headers out of range";
    case ELF_ERR_NOMEM:     return "out of memory";
    case ELF_ERR_MMAP:      return "segment mapping failed";
    default:                return "unknown error";
    }
}

static int elf_validate(const Elf64_Ehdr *eh, size_t size)
{
    if (size < sizeof(Elf64_Ehdr))                    return ELF_ERR_BADMAGIC;
    if (eh->e_ident[EI_MAG0] != ELF_MAGIC0 ||
        eh->e_ident[EI_MAG1] != ELF_MAGIC1 ||
        eh->e_ident[EI_MAG2] != ELF_MAGIC2 ||
        eh->e_ident[EI_MAG3] != ELF_MAGIC3)           return ELF_ERR_BADMAGIC;
    if (eh->e_ident[EI_CLASS] != ELFCLASS64)           return ELF_ERR_BADCLASS;
    if (eh->e_machine != EM_X86_64)                    return ELF_ERR_BADARCH;
    if (eh->e_type != ET_EXEC && eh->e_type != ET_DYN) return ELF_ERR_BADTYPE;

    uint64_t ph_end = (uint64_t)eh->e_phoff
                    + (uint64_t)eh->e_phentsize * eh->e_phnum;
    if (ph_end > size)                                 return ELF_ERR_BADPHOFF;

    return ELF_OK;
}

static int load_segments(const uint8_t *data, size_t data_size,
                         const Elf64_Ehdr *eh, vm_space_t *space)
{
    const Elf64_Phdr *phdrs = (const Elf64_Phdr *)(data + eh->e_phoff);

    for (Elf64_Half i = 0; i < eh->e_phnum; i++) {
        const Elf64_Phdr *ph = &phdrs[i];

        if (ph->p_type != PT_LOAD || ph->p_memsz == 0)
            continue;

        if (ph->p_offset + ph->p_filesz > data_size) {
            printk("elf: segment %u out of bounds\n", i);
            return ELF_ERR_MMAP;
        }

        uint64_t vstart = ph->p_vaddr & ~(uint64_t)(PAGE_SIZE - 1);
        uint64_t vend   = (ph->p_vaddr + ph->p_memsz + PAGE_SIZE - 1)
                          & ~(uint64_t)(PAGE_SIZE - 1);
        size_t   vsz    = (size_t)(vend - vstart);

        int ret = vmm_map_region(space, vstart, vsz,
                                 phdr_to_vmm_flags(ph->p_flags),
                                 VMM_TYPE_ANON, VMM_ALLOC_ZERO, 0);
        if (ret != 0) {
            printk("elf: vmm_map_region failed seg %u vaddr=0x%lx\n",
                   i, vstart);
            return ELF_ERR_MMAP;
        }

        uint64_t page_off  = ph->p_vaddr - vstart;   /* offset into first page */
        uint64_t src_off   = ph->p_offset;
        uint64_t remaining = ph->p_filesz;
        uint64_t vcur      = vstart;

        while (remaining > 0) {
            uint64_t phys = mmu_virt_to_phys(
                                (mmu_context_t *)space->mmu_ctx, vcur);
            if (phys == 0) {
                printk("elf: v2p failed at 0x%lx\n", vcur);
                return ELF_ERR_MMAP;
            }
            uint8_t *kpage   = (uint8_t *)PHYS_TO_VIRT(phys);
            uint64_t chunk   = PAGE_SIZE - page_off;
            if (chunk > remaining) chunk = remaining;
            memcpy(kpage + page_off, data + src_off, chunk);
            src_off   += chunk;
            remaining -= chunk;
            vcur      += PAGE_SIZE;
            page_off   = 0;   /* subsequent pages start at offset 0 */
        }
    }
    return ELF_OK;
}

static int setup_stack(vm_space_t *space, uint64_t *top_out)
{
    int ret = vmm_map_region(space,
                             USER_STACK_BOTTOM,
                             USER_STACK_PAGES * PAGE_SIZE,
                             VMM_USER | VMM_READ | VMM_WRITE,
                             VMM_TYPE_ANON, VMM_ALLOC_ZERO, 0);
    if (ret != 0) { printk("elf: stack map failed\n"); return ELF_ERR_NOMEM; }

    uint64_t rsp = USER_STACK_TOP - 24;

    uint64_t page  = rsp & ~(uint64_t)(PAGE_SIZE - 1);
    uint64_t phys  = mmu_virt_to_phys((mmu_context_t *)space->mmu_ctx, page);
    if (phys == 0) {
        printk("elf: stack v2p failed\n");
        return ELF_ERR_NOMEM;
    }

    uint64_t *kstack = (uint64_t *)PHYS_TO_VIRT(phys);
    uint64_t  off    = (rsp - page) / sizeof(uint64_t);

    kstack[off + 0] = 0;    /* argc = 0 */
    kstack[off + 1] = 0;    /* argv[0] = NULL */
    kstack[off + 2] = 0;    /* envp[0] = NULL */

    *top_out = rsp;
    return ELF_OK;
}

pcb_t *elf_load(const void *data, size_t size,
                const char *name, uint8_t priority, int *err_out)
{
    int err;
#define FAIL(c) do { err = (c); goto fail; } while (0)

    const Elf64_Ehdr *eh = (const Elf64_Ehdr *)data;

    err = elf_validate(eh, size);
    if (err != ELF_OK) { printk("elf: %s\n", elf_strerror(err)); goto fail; }

    vm_space_t *space = vmm_create_space();
    if (!space) FAIL(ELF_ERR_NOMEM);

    err = load_segments((const uint8_t *)data, size, eh, space);
    if (err != ELF_OK) { vmm_destroy_space(space); goto fail; }

    uint64_t stack_top;
    err = setup_stack(space, &stack_top);
    if (err != ELF_OK) { vmm_destroy_space(space); goto fail; }

    pcb_t *proc = proc_create_user_with_space(name, eh->e_entry,
                                              stack_top, priority, space);
    if (!proc) { vmm_destroy_space(space); FAIL(ELF_ERR_NOMEM); }

    for (int i = 0; i < 3; i++) {
        file_descriptor_t *fde =
            (file_descriptor_t *)kmalloc(sizeof(file_descriptor_t));
        if (fde == NULL) {
            proc_terminate(proc, -1);
            FAIL(ELF_ERR_NOMEM);
        }
        fde->file     = NULL;   /* sentinel: handled by syscall layer */
        fde->flags    = (i == 0) ? VFS_O_RDONLY : VFS_O_WRONLY;
        fde->offset   = 0;
        fde->refcount = 1;
        proc_fd_install(proc, i, fde);
    }

    {
        uint64_t heap_start = 0;
        const Elf64_Phdr *phdrs =
            (const Elf64_Phdr *)((const uint8_t *)data + eh->e_phoff);

        for (Elf64_Half i = 0; i < eh->e_phnum; i++) {
            const Elf64_Phdr *ph = &phdrs[i];
            if (ph->p_type != PT_LOAD || ph->p_memsz == 0)
                continue;
            uint64_t seg_end = PAGE_ALIGN_UP(ph->p_vaddr + ph->p_memsz);
            if (seg_end > heap_start)
                heap_start = seg_end;
        }

        if (heap_start == 0 || heap_start >= USER_STACK_BOTTOM) {
            printk("elf: bad heap_base 0x%lx\n", heap_start);
            proc_terminate(proc, -1);
            FAIL(ELF_ERR_MMAP);
        }

        proc->heap_base = heap_start;
        proc->heap_brk  = heap_start;
    }

    if (err_out) *err_out = ELF_OK;
    return proc;

fail:
    if (err_out) *err_out = err;
    return NULL;
#undef FAIL
}

pcb_t *elf_load_from_path(const char *path, const char *name,
                          uint8_t priority, int *err_out)
{
    /* stat to get size */
    vfs_stat_t st;
    if (vfs_stat(path, &st) != 0) {
        printk("elf: cannot stat '%s'\n", path);
        if (err_out) *err_out = ELF_ERR_BADMAGIC;
        return NULL;
    }

    if (st.st_size == 0) {
        if (err_out) *err_out = ELF_ERR_BADMAGIC;
        return NULL;
    }

    void *buf = kmalloc((size_t)st.st_size);
    if (!buf) {
        if (err_out) *err_out = ELF_ERR_NOMEM;
        return NULL;
    }

    vfs_file_t *f = NULL;
    if (vfs_open(path, VFS_O_RDONLY, 0, &f) != 0) {
        printk("elf: cannot open '%s'\n", path);
        kfree(buf);
        if (err_out) *err_out = ELF_ERR_BADMAGIC;
        return NULL;
    }

    size_t total = 0;
    while (total < (size_t)st.st_size) {
        int n = vfs_read(f, (uint8_t *)buf + total,
                         (size_t)st.st_size - total);
        if (n <= 0) break;
        total += (size_t)n;
    }
    vfs_close(f);

    if (total != (size_t)st.st_size) {
        printk("elf: short read %zu / %lu\n", total, (uint64_t)st.st_size);
        kfree(buf);
        if (err_out) *err_out = ELF_ERR_BADMAGIC;
        return NULL;
    }

    pcb_t *proc = elf_load(buf, total, name, priority, err_out);
    kfree(buf);
    return proc;
}