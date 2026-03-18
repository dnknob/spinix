#include <fs/elf_abi.h>
#include <fs/runit.h>
#include <fs/vfs.h>

#include <core/proc.h>
#include <core/scheduler.h>

#include <mm/heap.h>

#include <video/printk.h>

#include <klibc/string.h>

pcb_t *runit(const char *path, const char *name,
             uint8_t priority, int *err_out)
{
    if (path == NULL) {
        if (err_out) *err_out = -1;
        return NULL;
    }

    const char *proc_name = name ? name : path;
    pcb_t *proc = elf_load_from_path(path, proc_name, priority, err_out);
    if (proc == NULL) {
        printk("runit: failed to launch '%s'\n", path);
        return NULL;
    }
    return proc;
}

pcb_t *runit_buf(const void *data, size_t size, const char *name,
                 uint8_t priority, int *err_out)
{
    if (data == NULL || size == 0) {
        if (err_out) *err_out = -1;
        return NULL;
    }

    const char *proc_name = name ? name : "unknown";
    pcb_t *proc = elf_load(data, size, proc_name, priority, err_out);
    if (proc == NULL) {
        return NULL;
    }

    printk("runit: launched '%s' from buffer (pid %lu)\n",
           proc_name, proc->pid);
    return proc;
}

pcb_t *runit_embedded(const runit_embed_t *emb, const char *name,
                      uint8_t priority, int *err_out)
{
    if (emb == NULL || emb->start == NULL || emb->end == NULL) {
        if (err_out) *err_out = -1;
        return NULL;
    }

    size_t size = (size_t)(emb->end - emb->start);
    const char *proc_name = name ? name : (emb->name ? emb->name : "embedded");

    return runit_buf(emb->start, size, proc_name, priority, err_out);
}

int runit_wait(pcb_t *proc)
{
    if (proc == NULL)
        return -1;

    while (proc->state != PROC_STATE_TERMINATED &&
           proc->state != PROC_STATE_ZOMBIE) {
        yield();
    }

    int code = proc->exit_code;
    return code;
}

int runit_done(pcb_t *proc)
{
    if (proc == NULL)
        return 1;
    return (proc->state == PROC_STATE_TERMINATED ||
            proc->state == PROC_STATE_ZOMBIE) ? 1 : 0;
}

int runit_install(const char *vfs_path,
                  const uint8_t *data, size_t size)
{
    if (vfs_path == NULL || data == NULL || size == 0)
        return -1;

    char dir[VFS_PATH_MAX];
    char filename[VFS_NAME_MAX + 1];
    vfs_path_split(vfs_path, dir, filename);

    if (strcmp(dir, "/") != 0 && strcmp(dir, ".") != 0) {
        /* walk and mkdir -p each component */
        char tmp[VFS_PATH_MAX];
        size_t len = strlen(dir);
        memcpy(tmp, dir, len + 1);

        for (size_t i = 1; i <= len; i++) {
            if (tmp[i] == '/' || tmp[i] == '\0') {
                char save = tmp[i];
                tmp[i] = '\0';
                vfs_mkdir(tmp, 0755); /* ignore EEXIST */
                tmp[i] = save;
            }
        }
    }

    vfs_file_t *f = NULL;
    int ret = vfs_create(vfs_path, 0755, &f);
    if (ret != 0) {
        printk("runit: install: cannot create '%s': %d\n", vfs_path, ret);
        return ret;
    }

    size_t written = 0;
    while (written < size) {
        int n = vfs_write(f, data + written, size - written);
        if (n <= 0) {
            printk("runit: install: write error at offset %zu\n", written);
            vfs_close(f);
            return -1;
        }
        written += (size_t)n;
    }

    vfs_close(f);
    return 0;
}