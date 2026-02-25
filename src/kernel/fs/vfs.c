#include <core/spinlock.h>
#include <core/mutex.h>
#include <core/proc.h>

#include <blk/blk.h>

#include <fs/vfs.h>

#include <mm/heap.h>

#include <video/printk.h>

#include <klibc/string.h>
#include <errno.h>

static struct {
    vfs_mount_t *mount_list;
    vnode_t *vnode_list;
    vfs_mount_t *root_mount;

    const vfs_filesystem_ops_t *fs_types[16];
    uint32_t num_fs_types;

    spinlock_irq_t mount_lock;
    spinlock_irq_t vnode_lock;
    spinlock_irq_t fs_type_lock;

    uint64_t stat_lookups;
    uint64_t stat_opens;
    uint64_t stat_reads;
    uint64_t stat_writes;
    uint64_t stat_vnodes_allocated;
    uint64_t stat_vnodes_freed;

    bool initialized;
} vfs_state = {0};

static inline uint64_t get_time_seconds(void) {
    return 0;
}

static void vnode_init_common(vnode_t *vnode, vfs_mount_t *mount, uint32_t type) {
    vnode->v_type = type;
    vnode->v_mount = mount;
    vnode->v_refcount = 1;
    vnode->v_nlink = 1;
    vnode->v_uid = 0;
    vnode->v_gid = 0;
    vnode->v_size = 0;

    uint64_t now = get_time_seconds();
    vnode->v_atime = now;
    vnode->v_mtime = now;
    vnode->v_ctime = now;

    vnode->v_parent = NULL;
    vnode->v_next = NULL;
    vnode->v_data = NULL;
}

void vfs_init(void) {
    if (vfs_state.initialized)
        return;

    memset(&vfs_state, 0, sizeof(vfs_state));

    spinlock_irq_init(&vfs_state.mount_lock);
    spinlock_irq_init(&vfs_state.vnode_lock);
    spinlock_irq_init(&vfs_state.fs_type_lock);

    vfs_state.mount_list = NULL;
    vfs_state.vnode_list = NULL;
    vfs_state.root_mount = NULL;
    vfs_state.num_fs_types = 0;

    vfs_state.initialized = true;

    printk("vfs: initialized\n");
}

int vfs_register_filesystem(const vfs_filesystem_ops_t *fs_ops) {
    if (!vfs_state.initialized || fs_ops == NULL || fs_ops->fs_name == NULL)
        return -EINVAL;

    spinlock_irq_acquire(&vfs_state.fs_type_lock);

    for (uint32_t i = 0; i < vfs_state.num_fs_types; i++) {
        if (strcmp(vfs_state.fs_types[i]->fs_name, fs_ops->fs_name) == 0) {
            spinlock_irq_release(&vfs_state.fs_type_lock);
            return -EEXIST;
        }
    }

    if (vfs_state.num_fs_types >= 16) {
        spinlock_irq_release(&vfs_state.fs_type_lock);
        return -ENOMEM;
    }

    vfs_state.fs_types[vfs_state.num_fs_types++] = fs_ops;

    spinlock_irq_release(&vfs_state.fs_type_lock);

    printk("vfs: registered filesystem type '%s'\n", fs_ops->fs_name);
    return 0;
}

int vfs_unregister_filesystem(const char *fs_name) {
    if (!vfs_state.initialized || fs_name == NULL)
        return -EINVAL;

    spinlock_irq_acquire(&vfs_state.fs_type_lock);

    for (uint32_t i = 0; i < vfs_state.num_fs_types; i++) {
        if (strcmp(vfs_state.fs_types[i]->fs_name, fs_name) == 0) {
            for (uint32_t j = i; j < vfs_state.num_fs_types - 1; j++)
                vfs_state.fs_types[j] = vfs_state.fs_types[j + 1];
            vfs_state.num_fs_types--;

            spinlock_irq_release(&vfs_state.fs_type_lock);
            printk("vfs: unregistered filesystem type '%s'\n", fs_name);
            return 0;
        }
    }

    spinlock_irq_release(&vfs_state.fs_type_lock);
    return -ENOENT;
}

static const vfs_filesystem_ops_t *vfs_find_filesystem(const char *fs_name) {
    spinlock_irq_acquire(&vfs_state.fs_type_lock);

    for (uint32_t i = 0; i < vfs_state.num_fs_types; i++) {
        if (strcmp(vfs_state.fs_types[i]->fs_name, fs_name) == 0) {
            const vfs_filesystem_ops_t *ops = vfs_state.fs_types[i];
            spinlock_irq_release(&vfs_state.fs_type_lock);
            return ops;
        }
    }

    spinlock_irq_release(&vfs_state.fs_type_lock);
    return NULL;
}

int vfs_mount(const char *device, const char *mountpoint, const char *fstype, uint32_t flags) {
    if (!vfs_state.initialized || fstype == NULL || mountpoint == NULL)
        return -EINVAL;

    const vfs_filesystem_ops_t *fs_ops = vfs_find_filesystem(fstype);
    if (fs_ops == NULL) {
        printk("vfs: unknown filesystem type '%s'\n", fstype);
        return -ENODEV;
    }

    struct blk_device *blk_dev = NULL;
    if (device != NULL) {
        blk_dev = blk_find_device_by_name(device);
        if (blk_dev == NULL) {
            printk("vfs: device '%s' not found\n", device);
            return -ENODEV;
        }

        int ret = blk_open(blk_dev);
        if (ret != 0)
            return ret;
    }

    vfs_mount_t *mount = NULL;
    int ret = fs_ops->mount(blk_dev, flags, &mount);
    if (ret != 0) {
        if (blk_dev != NULL)
            blk_close(blk_dev);
        return ret;
    }

    strncpy(mount->mnt_path, mountpoint, VFS_PATH_MAX - 1);
    mount->mnt_path[VFS_PATH_MAX - 1] = '\0';
    mount->mnt_flags = flags;
    mount->mnt_dev = blk_dev;
    mount->mnt_ops = fs_ops;
    mount->mnt_refcount = 1;

    if (strcmp(mountpoint, "/") == 0) {
        mount->mnt_covered = NULL;
        vfs_state.root_mount = mount;
    } else {
        vnode_t *covered = NULL;
        ret = vfs_lookup(mountpoint, &covered);
        if (ret != 0) {
            fs_ops->unmount(mount);
            if (blk_dev != NULL)
                blk_close(blk_dev);
            return ret;
        }

        if (covered->v_type != VFS_TYPE_DIR) {
            vfs_vnode_unref(covered);
            fs_ops->unmount(mount);
            if (blk_dev != NULL)
                blk_close(blk_dev);
            return -ENOTDIR;
        }

        mount->mnt_covered = covered;
    }

    spinlock_irq_acquire(&vfs_state.mount_lock);
    mount->mnt_next = vfs_state.mount_list;
    vfs_state.mount_list = mount;
    spinlock_irq_release(&vfs_state.mount_lock);

    printk("vfs: mounted %s on %s (type %s)\n",
           device ? device : "none", mountpoint, fstype);

    return 0;
}

int vfs_unmount(const char *mountpoint) {
    if (!vfs_state.initialized || mountpoint == NULL)
        return -EINVAL;

    spinlock_irq_acquire(&vfs_state.mount_lock);

    vfs_mount_t **prev = &vfs_state.mount_list;
    vfs_mount_t *mount = vfs_state.mount_list;

    while (mount != NULL) {
        if (strcmp(mount->mnt_path, mountpoint) == 0) {
            if (mount->mnt_refcount > 1) {
                spinlock_irq_release(&vfs_state.mount_lock);
                return -EBUSY;
            }

            *prev = mount->mnt_next;
            spinlock_irq_release(&vfs_state.mount_lock);

            if (mount->mnt_ops->unmount)
                mount->mnt_ops->unmount(mount);

            if (mount->mnt_dev != NULL)
                blk_close(mount->mnt_dev);

            if (mount->mnt_covered != NULL)
                vfs_vnode_unref(mount->mnt_covered);

            kfree(mount);

            printk("vfs: unmounted %s\n", mountpoint);
            return 0;
        }

        prev = &mount->mnt_next;
        mount = mount->mnt_next;
    }

    spinlock_irq_release(&vfs_state.mount_lock);
    return -ENOENT;
}

vfs_mount_t *vfs_find_mount(const char *path) {
    if (!vfs_state.initialized || path == NULL)
        return NULL;

    spinlock_irq_acquire(&vfs_state.mount_lock);

    vfs_mount_t *best_mount = NULL;
    size_t best_len = 0;

    vfs_mount_t *mount = vfs_state.mount_list;
    while (mount != NULL) {
        size_t len = strlen(mount->mnt_path);

        if (strncmp(path, mount->mnt_path, len) == 0) {
            if (len > best_len || (len == best_len && best_mount == NULL)) {
                best_mount = mount;
                best_len = len;
            }
        }

        mount = mount->mnt_next;
    }

    spinlock_irq_release(&vfs_state.mount_lock);
    return best_mount ? best_mount : vfs_state.root_mount;
}

vnode_t *vfs_vnode_alloc(vfs_mount_t *mount) {
    if (mount == NULL)
        return NULL;

    vnode_t *vnode = (vnode_t *)kmalloc(sizeof(vnode_t));
    if (vnode == NULL)
        return NULL;

    memset(vnode, 0, sizeof(vnode_t));
    vnode_init_common(vnode, mount, VFS_TYPE_FILE);

    spinlock_irq_acquire(&vfs_state.vnode_lock);
    vnode->v_next = vfs_state.vnode_list;
    vfs_state.vnode_list = vnode;
    vfs_state.stat_vnodes_allocated++;
    spinlock_irq_release(&vfs_state.vnode_lock);

    return vnode;
}

void vfs_vnode_ref(vnode_t *vnode) {
    if (vnode == NULL)
        return;

    __sync_fetch_and_add(&vnode->v_refcount, 1);
}

void vfs_vnode_unref(vnode_t *vnode) {
    if (vnode == NULL)
        return;

    uint32_t old_ref = __sync_fetch_and_sub(&vnode->v_refcount, 1);

    if (old_ref == 1) {
        if (vnode->v_ops && vnode->v_ops->release)
            vnode->v_ops->release(vnode);

        spinlock_irq_acquire(&vfs_state.vnode_lock);

        vnode_t **prev = &vfs_state.vnode_list;
        vnode_t *curr = vfs_state.vnode_list;

        while (curr != NULL) {
            if (curr == vnode) {
                *prev = curr->v_next;
                break;
            }
            prev = &curr->v_next;
            curr = curr->v_next;
        }

        vfs_state.stat_vnodes_freed++;
        spinlock_irq_release(&vfs_state.vnode_lock);

        kfree(vnode);
    }
}

bool vfs_path_is_absolute(const char *path) {
    return path != NULL && path[0] == '/';
}

/* Working buffer size — must be > any path we'll ever use in the shell */
#define VFS_WORK_BUF    512
#define VFS_WORK_COMP   32     /* max path components */

int vfs_path_normalize(const char *path, char *normalized, size_t size) {
    if (path == NULL || normalized == NULL || size == 0)
        return -EINVAL;

    if (path[0] == '\0') {
        if (size < 2) return -ENAMETOOLONG;
        strcpy(normalized, ".");
        return 0;
    }

    char *temp = (char *)kmalloc(VFS_WORK_BUF);
    if (temp == NULL) return -ENOMEM;

    char **components = (char **)kmalloc(VFS_WORK_COMP * sizeof(char *));
    if (components == NULL) { kfree(temp); return -ENOMEM; }

    int num_components = 0;

    strncpy(temp, path, VFS_WORK_BUF - 1);
    temp[VFS_WORK_BUF - 1] = '\0';

    char *token = temp;
    char *next  = temp;

    while (*next != '\0') {
        if (*next == '/') {
            *next = '\0';
            if (token != next && token[0] != '\0') {
                if (strcmp(token, ".") == 0) {
                    /* skip */
                } else if (strcmp(token, "..") == 0) {
                    if (num_components > 0) num_components--;
                } else if (num_components < VFS_WORK_COMP) {
                    components[num_components++] = token;
                }
            }
            token = next + 1;
        }
        next++;
    }

    if (token[0] != '\0') {
        if (strcmp(token, ".") != 0) {
            if (strcmp(token, "..") == 0) {
                if (num_components > 0) num_components--;
            } else if (num_components < VFS_WORK_COMP) {
                components[num_components++] = token;
            }
        }
    }

    size_t offset = 0;

    if (vfs_path_is_absolute(path)) {
        if (offset + 1 >= size) { kfree(temp); kfree(components); return -ENAMETOOLONG; }
        normalized[offset++] = '/';
    }

    for (int i = 0; i < num_components; i++) {
        size_t len = strlen(components[i]);
        if (i > 0) {
            if (offset + 1 >= size) { kfree(temp); kfree(components); return -ENAMETOOLONG; }
            normalized[offset++] = '/';
        }
        if (offset + len >= size) { kfree(temp); kfree(components); return -ENAMETOOLONG; }
        strcpy(&normalized[offset], components[i]);
        offset += len;
    }

    if (offset == 0) {
        if (vfs_path_is_absolute(path)) {
            normalized[0] = '/'; normalized[1] = '\0';
        } else {
            normalized[0] = '.'; normalized[1] = '\0';
        }
    } else {
        normalized[offset] = '\0';
    }

    kfree(temp);
    kfree(components);
    return 0;
}

int vfs_path_split(const char *path, char *dir, char *name) {
    if (path == NULL)
        return -EINVAL;

    const char *last_slash = NULL;
    const char *p = path;

    while (*p != '\0') {
        if (*p == '/') last_slash = p;
        p++;
    }

    if (last_slash == NULL) {
        if (dir  != NULL) strcpy(dir, ".");
        if (name != NULL) strcpy(name, path);
    } else if (last_slash == path) {
        if (dir  != NULL) strcpy(dir, "/");
        if (name != NULL) strcpy(name, last_slash + 1);
    } else {
        if (dir != NULL) {
            size_t dir_len = last_slash - path;
            strncpy(dir, path, dir_len);
            dir[dir_len] = '\0';
        }
        if (name != NULL)
            strcpy(name, last_slash + 1);
    }

    return 0;
}

int vfs_path_join(const char *dir, const char *name, char *result, size_t size) {
    if (dir == NULL || name == NULL || result == NULL || size == 0)
        return -EINVAL;

    size_t dir_len  = strlen(dir);
    size_t name_len = strlen(name);

    bool needs_sep = (dir_len > 0 && dir[dir_len - 1] != '/');
    size_t total_len = dir_len + (needs_sep ? 1 : 0) + name_len;

    if (total_len >= size)
        return -ENAMETOOLONG;

    strcpy(result, dir);
    if (needs_sep) {
        result[dir_len]     = '/';
        result[dir_len + 1] = '\0';
    }
    strcat(result, name);

    return 0;
}

int vfs_lookup(const char *path, vnode_t **result) {
    if (!vfs_state.initialized || path == NULL || result == NULL)
        return -EINVAL;

    vfs_state.stat_lookups++;

    char *normalized = (char *)kmalloc(VFS_WORK_BUF);
    if (normalized == NULL) return -ENOMEM;

    int ret = vfs_path_normalize(path, normalized, VFS_WORK_BUF);
    if (ret != 0) { kfree(normalized); return ret; }

    vnode_t *current = NULL;

    if (vfs_path_is_absolute(normalized)) {
        if (vfs_state.root_mount == NULL || vfs_state.root_mount->mnt_root == NULL) {
            kfree(normalized);
            return -ENOENT;
        }
        current = vfs_state.root_mount->mnt_root;
        vfs_vnode_ref(current);
    } else {
        pcb_t *proc = proc_get_current();
        if (proc == NULL) { kfree(normalized); return -EINVAL; }
        ret = vfs_lookup(proc->cwd, &current);
        if (ret != 0) { kfree(normalized); return ret; }
    }

    if (strcmp(normalized, "/") == 0) {
        *result = current;
        kfree(normalized);
        return 0;
    }

    char *temp = (char *)kmalloc(VFS_WORK_BUF);
    if (temp == NULL) { vfs_vnode_unref(current); kfree(normalized); return -ENOMEM; }

    strncpy(temp, normalized, VFS_WORK_BUF - 1);
    temp[VFS_WORK_BUF - 1] = '\0';
    kfree(normalized);

    char *token = temp;
    if (token[0] == '/') token++;

    char *next = token;

    while (*token != '\0') {
        while (*next != '\0' && *next != '/') next++;

        bool is_last = (*next == '\0');
        if (!is_last) { *next = '\0'; next++; }

        if (token[0] == '\0') { token = next; continue; }

        if (current->v_type != VFS_TYPE_DIR) {
            vfs_vnode_unref(current);
            kfree(temp);
            return -ENOTDIR;
        }

        if (current->v_ops == NULL || current->v_ops->lookup == NULL) {
            vfs_vnode_unref(current);
            kfree(temp);
            return -ENOTSUP;
        }

        vnode_t *next_vnode = NULL;
        ret = current->v_ops->lookup(current, token, &next_vnode);
        vfs_vnode_unref(current);

        if (ret != 0) { kfree(temp); return ret; }

        spinlock_irq_acquire(&vfs_state.mount_lock);
        vfs_mount_t *mnt = vfs_state.mount_list;
        while (mnt != NULL) {
            if (mnt->mnt_covered == next_vnode && mnt->mnt_root != NULL) {
                vfs_vnode_ref(mnt->mnt_root);
                vfs_vnode_unref(next_vnode);
                next_vnode = mnt->mnt_root;
                break;
            }
            mnt = mnt->mnt_next;
        }
        spinlock_irq_release(&vfs_state.mount_lock);

        current = next_vnode;
        token   = next;
    }

    *result = current;
    kfree(temp);
    return 0;
}

int vfs_lookup_parent(const char *path, vnode_t **parent, char *name) {
    if (!vfs_state.initialized || path == NULL || parent == NULL)
        return -EINVAL;

    char *dir = (char *)kmalloc(VFS_WORK_BUF);
    if (dir == NULL) return -ENOMEM;

    char *filename = (char *)kmalloc(VFS_NAME_MAX + 1);
    if (filename == NULL) { kfree(dir); return -ENOMEM; }

    int ret = vfs_path_split(path, dir, filename);
    if (ret != 0) { kfree(dir); kfree(filename); return ret; }

    if (name != NULL) {
        strncpy(name, filename, VFS_NAME_MAX);
        name[VFS_NAME_MAX] = '\0';
    }

    ret = vfs_lookup(dir, parent);

    kfree(dir);
    kfree(filename);
    return ret;
}

int vfs_open(const char *path, uint32_t flags, uint32_t mode, vfs_file_t **file) {
    if (!vfs_state.initialized || path == NULL || file == NULL)
        return -EINVAL;

    vfs_state.stat_opens++;

    vnode_t *vnode = NULL;
    int ret = vfs_lookup(path, &vnode);

    if (ret == -ENOENT && (flags & VFS_O_CREAT)) {
        vnode_t *parent = NULL;
        char name[VFS_NAME_MAX + 1];

        ret = vfs_lookup_parent(path, &parent, name);
        if (ret != 0)
            return ret;

        if (parent->v_ops == NULL || parent->v_ops->create == NULL) {
            vfs_vnode_unref(parent);
            return -ENOTSUP;
        }

        ret = parent->v_ops->create(parent, name, mode, &vnode);
        vfs_vnode_unref(parent);

        if (ret != 0)
            return ret;
    } else if (ret != 0) {
        return ret;
    }

    if ((flags & (VFS_O_CREAT | VFS_O_EXCL)) == (VFS_O_CREAT | VFS_O_EXCL)) {
        vfs_vnode_unref(vnode);
        return -EEXIST;
    }

    if (vnode->v_type == VFS_TYPE_DIR && !(flags & VFS_O_DIRECTORY)) {
        vfs_vnode_unref(vnode);
        return -EISDIR;
    }

    if (vnode->v_type != VFS_TYPE_DIR && (flags & VFS_O_DIRECTORY)) {
        vfs_vnode_unref(vnode);
        return -ENOTDIR;
    }

    if (vnode->v_ops && vnode->v_ops->open) {
        ret = vnode->v_ops->open(vnode, flags);
        if (ret != 0) {
            vfs_vnode_unref(vnode);
            return ret;
        }
    }

    if ((flags & VFS_O_TRUNC) && vnode->v_type == VFS_TYPE_FILE) {
        if (vnode->v_ops && vnode->v_ops->truncate) {
            ret = vnode->v_ops->truncate(vnode, 0);
            if (ret != 0) {
                vfs_vnode_unref(vnode);
                return ret;
            }
        }
    }

    vfs_file_t *f = (vfs_file_t *)kmalloc(sizeof(vfs_file_t));
    if (f == NULL) {
        vfs_vnode_unref(vnode);
        return -ENOMEM;
    }

    f->f_vnode   = vnode;
    f->f_flags   = flags;
    f->f_offset  = 0;
    f->f_refcount = 1;
    f->f_private = NULL;

    *file = f;
    return 0;
}

int vfs_close(vfs_file_t *file) {
    if (file == NULL)
        return -EINVAL;

    uint32_t old_ref = __sync_fetch_and_sub(&file->f_refcount, 1);

    if (old_ref == 1) {
        vnode_t *vnode = file->f_vnode;

        if (vnode != NULL) {
            if (vnode->v_ops && vnode->v_ops->close)
                vnode->v_ops->close(vnode);
            vfs_vnode_unref(vnode);
        }

        kfree(file);
    }

    return 0;
}

int vfs_read(vfs_file_t *file, void *buf, size_t len) {
    if (file == NULL || buf == NULL)
        return -EINVAL;

    vfs_state.stat_reads++;

    vnode_t *vnode = file->f_vnode;
    if (vnode == NULL) return -EBADF;

    if ((file->f_flags & VFS_O_ACCMODE) == VFS_O_WRONLY)
        return -EBADF;

    if (vnode->v_ops == NULL || vnode->v_ops->read == NULL)
        return -ENOTSUP;

    int ret = vnode->v_ops->read(vnode, buf, len, file->f_offset);
    if (ret > 0)
        file->f_offset += ret;

    return ret;
}

int vfs_write(vfs_file_t *file, const void *buf, size_t len) {
    if (file == NULL || buf == NULL)
        return -EINVAL;

    vfs_state.stat_writes++;

    vnode_t *vnode = file->f_vnode;
    if (vnode == NULL) return -EBADF;

    uint32_t mode = file->f_flags & VFS_O_ACCMODE;
    if (mode == VFS_O_RDONLY)
        return -EBADF;

    if (vnode->v_mount && (vnode->v_mount->mnt_flags & VFS_MNT_RDONLY))
        return -EROFS;

    if (vnode->v_ops == NULL || vnode->v_ops->write == NULL)
        return -ENOTSUP;

    uint64_t write_offset = file->f_offset;
    if (file->f_flags & VFS_O_APPEND)
        write_offset = vnode->v_size;

    int ret = vnode->v_ops->write(vnode, buf, len, write_offset);
    if (ret > 0)
        file->f_offset = write_offset + ret;

    return ret;
}

int64_t vfs_lseek(vfs_file_t *file, int64_t offset, int whence) {
    if (file == NULL) return -EINVAL;

    vnode_t *vnode = file->f_vnode;
    if (vnode == NULL) return -EBADF;

    uint64_t new_offset;

    switch (whence) {
        case VFS_SEEK_SET: new_offset = offset; break;
        case VFS_SEEK_CUR: new_offset = file->f_offset + offset; break;
        case VFS_SEEK_END: new_offset = vnode->v_size + offset; break;
        default: return -EINVAL;
    }

    if (offset > 0 && new_offset < file->f_offset)
        return -EOVERFLOW;

    file->f_offset = new_offset;
    return new_offset;
}

int vfs_stat(const char *path, vfs_stat_t *stat) {
    if (!vfs_state.initialized || path == NULL || stat == NULL)
        return -EINVAL;

    vnode_t *vnode = NULL;
    int ret = vfs_lookup(path, &vnode);
    if (ret != 0) return ret;

    if (vnode->v_ops && vnode->v_ops->getattr) {
        ret = vnode->v_ops->getattr(vnode, stat);
    } else {
        memset(stat, 0, sizeof(vfs_stat_t));
        stat->st_ino   = vnode->v_ino;
        stat->st_mode  = vnode->v_mode;
        stat->st_nlink = vnode->v_nlink;
        stat->st_uid   = vnode->v_uid;
        stat->st_gid   = vnode->v_gid;
        stat->st_size  = vnode->v_size;
        stat->st_atime = vnode->v_atime;
        stat->st_mtime = vnode->v_mtime;
        stat->st_ctime = vnode->v_ctime;
        ret = 0;
    }

    vfs_vnode_unref(vnode);
    return ret;
}

int vfs_fstat(vfs_file_t *file, vfs_stat_t *stat) {
    if (file == NULL || stat == NULL) return -EINVAL;

    vnode_t *vnode = file->f_vnode;
    if (vnode == NULL) return -EBADF;

    if (vnode->v_ops && vnode->v_ops->getattr)
        return vnode->v_ops->getattr(vnode, stat);

    memset(stat, 0, sizeof(vfs_stat_t));
    stat->st_ino   = vnode->v_ino;
    stat->st_mode  = vnode->v_mode;
    stat->st_nlink = vnode->v_nlink;
    stat->st_uid   = vnode->v_uid;
    stat->st_gid   = vnode->v_gid;
    stat->st_size  = vnode->v_size;
    stat->st_atime = vnode->v_atime;
    stat->st_mtime = vnode->v_mtime;
    stat->st_ctime = vnode->v_ctime;

    return 0;
}

int vfs_truncate(const char *path, uint64_t size) {
    if (!vfs_state.initialized || path == NULL)
        return -EINVAL;

    vnode_t *vnode = NULL;
    int ret = vfs_lookup(path, &vnode);
    if (ret != 0) return ret;

    if (vnode->v_type != VFS_TYPE_FILE) { vfs_vnode_unref(vnode); return -EINVAL; }
    if (vnode->v_ops == NULL || vnode->v_ops->truncate == NULL) {
        vfs_vnode_unref(vnode); return -ENOTSUP;
    }

    ret = vnode->v_ops->truncate(vnode, size);
    vfs_vnode_unref(vnode);
    return ret;
}

int vfs_ftruncate(vfs_file_t *file, uint64_t size) {
    if (file == NULL) return -EINVAL;

    vnode_t *vnode = file->f_vnode;
    if (vnode == NULL) return -EBADF;
    if (vnode->v_type != VFS_TYPE_FILE) return -EINVAL;
    if (vnode->v_ops == NULL || vnode->v_ops->truncate == NULL) return -ENOTSUP;

    return vnode->v_ops->truncate(vnode, size);
}

int vfs_sync(vfs_file_t *file) {
    if (file == NULL) return -EINVAL;

    vnode_t *vnode = file->f_vnode;
    if (vnode == NULL) return -EBADF;

    if (vnode->v_ops && vnode->v_ops->sync)
        return vnode->v_ops->sync(vnode);

    return 0;
}

int vfs_ioctl(vfs_file_t *file, unsigned int cmd, unsigned long arg) {
    if (file == NULL) return -EINVAL;

    vnode_t *vnode = file->f_vnode;
    if (vnode == NULL) return -EBADF;

    if (vnode->v_ops && vnode->v_ops->ioctl)
        return vnode->v_ops->ioctl(vnode, cmd, arg);

    return -ENOTTY;
}

int vfs_mkdir(const char *path, uint32_t mode) {
    if (!vfs_state.initialized || path == NULL)
        return -EINVAL;

    vnode_t *parent = NULL;
    char name[VFS_NAME_MAX + 1];

    int ret = vfs_lookup_parent(path, &parent, name);
    if (ret != 0) return ret;

    if (parent->v_type != VFS_TYPE_DIR) {
        vfs_vnode_unref(parent); return -ENOTDIR;
    }
    if (parent->v_ops == NULL || parent->v_ops->mkdir == NULL) {
        vfs_vnode_unref(parent); return -ENOTSUP;
    }

    vnode_t *new_dir = NULL;
    ret = parent->v_ops->mkdir(parent, name, mode, &new_dir);
    vfs_vnode_unref(parent);

    if (ret == 0 && new_dir != NULL)
        vfs_vnode_unref(new_dir);

    return ret;
}

int vfs_rmdir(const char *path) {
    if (!vfs_state.initialized || path == NULL)
        return -EINVAL;

    vnode_t *parent = NULL;
    char name[VFS_NAME_MAX + 1];

    int ret = vfs_lookup_parent(path, &parent, name);
    if (ret != 0) return ret;

    if (parent->v_ops == NULL || parent->v_ops->rmdir == NULL) {
        vfs_vnode_unref(parent); return -ENOTSUP;
    }

    ret = parent->v_ops->rmdir(parent, name);
    vfs_vnode_unref(parent);
    return ret;
}

int vfs_readdir(vfs_file_t *file, vfs_dirent_t *dirent) {
    if (file == NULL || dirent == NULL) return -EINVAL;

    vnode_t *vnode = file->f_vnode;
    if (vnode == NULL) return -EBADF;
    if (vnode->v_type != VFS_TYPE_DIR) return -ENOTDIR;
    if (vnode->v_ops == NULL || vnode->v_ops->readdir == NULL) return -ENOTSUP;

    return vnode->v_ops->readdir(vnode, dirent, &file->f_offset);
}

int vfs_create(const char *path, uint32_t mode, vfs_file_t **file) {
    return vfs_open(path, VFS_O_CREAT | VFS_O_WRONLY | VFS_O_TRUNC, mode, file);
}

int vfs_link(const char *oldpath, const char *newpath) {
    if (!vfs_state.initialized || oldpath == NULL || newpath == NULL)
        return -EINVAL;

    vnode_t *target = NULL;
    int ret = vfs_lookup(oldpath, &target);
    if (ret != 0) return ret;

    if (target->v_type == VFS_TYPE_DIR) {
        vfs_vnode_unref(target); return -EPERM;
    }

    vnode_t *parent = NULL;
    char name[VFS_NAME_MAX + 1];

    ret = vfs_lookup_parent(newpath, &parent, name);
    if (ret != 0) { vfs_vnode_unref(target); return ret; }

    if (parent->v_ops == NULL || parent->v_ops->link == NULL) {
        vfs_vnode_unref(target); vfs_vnode_unref(parent); return -ENOTSUP;
    }

    ret = parent->v_ops->link(parent, name, target);
    vfs_vnode_unref(target);
    vfs_vnode_unref(parent);
    return ret;
}

int vfs_unlink(const char *path) {
    if (!vfs_state.initialized || path == NULL)
        return -EINVAL;

    vnode_t *parent = NULL;
    char name[VFS_NAME_MAX + 1];

    int ret = vfs_lookup_parent(path, &parent, name);
    if (ret != 0) return ret;

    if (parent->v_ops == NULL || parent->v_ops->unlink == NULL) {
        vfs_vnode_unref(parent); return -ENOTSUP;
    }

    ret = parent->v_ops->unlink(parent, name);
    vfs_vnode_unref(parent);
    return ret;
}

int vfs_symlink(const char *target, const char *linkpath) {
    if (!vfs_state.initialized || target == NULL || linkpath == NULL)
        return -EINVAL;

    vnode_t *parent = NULL;
    char name[VFS_NAME_MAX + 1];

    int ret = vfs_lookup_parent(linkpath, &parent, name);
    if (ret != 0) return ret;

    if (parent->v_ops == NULL || parent->v_ops->symlink == NULL) {
        vfs_vnode_unref(parent); return -ENOTSUP;
    }

    ret = parent->v_ops->symlink(parent, name, target);
    vfs_vnode_unref(parent);
    return ret;
}

int vfs_readlink(const char *path, char *buf, size_t bufsize) {
    if (!vfs_state.initialized || path == NULL || buf == NULL)
        return -EINVAL;

    vnode_t *vnode = NULL;
    int ret = vfs_lookup(path, &vnode);
    if (ret != 0) return ret;

    if (vnode->v_type != VFS_TYPE_SYMLINK) {
        vfs_vnode_unref(vnode); return -EINVAL;
    }
    if (vnode->v_ops == NULL || vnode->v_ops->readlink == NULL) {
        vfs_vnode_unref(vnode); return -ENOTSUP;
    }

    ret = vnode->v_ops->readlink(vnode, buf, bufsize);
    vfs_vnode_unref(vnode);
    return ret;
}

int vfs_rename(const char *oldpath, const char *newpath) {
    if (!vfs_state.initialized || oldpath == NULL || newpath == NULL)
        return -EINVAL;

    char old_name[VFS_NAME_MAX + 1];
    char new_name[VFS_NAME_MAX + 1];

    vnode_t *old_parent = NULL;
    vnode_t *new_parent = NULL;

    int ret = vfs_lookup_parent(oldpath, &old_parent, old_name);
    if (ret != 0) return ret;

    ret = vfs_lookup_parent(newpath, &new_parent, new_name);
    if (ret != 0) { vfs_vnode_unref(old_parent); return ret; }

    if (old_parent->v_ops == NULL || old_parent->v_ops->rename == NULL) {
        vfs_vnode_unref(old_parent); vfs_vnode_unref(new_parent); return -ENOTSUP;
    }

    ret = old_parent->v_ops->rename(old_parent, old_name, new_parent, new_name);
    vfs_vnode_unref(old_parent);
    vfs_vnode_unref(new_parent);
    return ret;
}

int vfs_chdir(const char *path) {
    if (!vfs_state.initialized || path == NULL)
        return -EINVAL;

    vnode_t *vnode = NULL;
    int ret = vfs_lookup(path, &vnode);
    if (ret != 0) return ret;

    if (vnode->v_type != VFS_TYPE_DIR) {
        vfs_vnode_unref(vnode); return -ENOTDIR;
    }

    pcb_t *proc = proc_get_current();
    if (proc == NULL) { vfs_vnode_unref(vnode); return -EINVAL; }

    char *normalized = (char *)kmalloc(512);
    if (normalized == NULL) { vfs_vnode_unref(vnode); return -ENOMEM; }

    if (vfs_path_is_absolute(path)) {
        ret = vfs_path_normalize(path, normalized, 512);
    } else {
        char *abspath = (char *)kmalloc(512);
        if (abspath == NULL) {
            kfree(normalized); vfs_vnode_unref(vnode); return -ENOMEM;
        }

        const char *cwd = proc_get_cwd(proc);
        ret = vfs_path_join(cwd ? cwd : "/", path, abspath, 512);
        if (ret == 0)
            ret = vfs_path_normalize(abspath, normalized, 512);
        kfree(abspath);
    }

    if (ret != 0) {
        kfree(normalized); vfs_vnode_unref(vnode); return ret;
    }

    ret = proc_set_cwd(proc, normalized);

    kfree(normalized);
    vfs_vnode_unref(vnode);
    return ret;
}

int vfs_getcwd(char *buf, size_t size) {
    if (buf == NULL || size == 0) return -EINVAL;

    pcb_t *proc = proc_get_current();
    if (proc == NULL) return -EINVAL;

    const char *cwd = proc_get_cwd(proc);
    if (cwd == NULL) return -EINVAL;

    size_t len = strlen(cwd);
    if (len >= size) return -ERANGE;

    strcpy(buf, cwd);
    return 0;
}

int vfs_access(const char *path, uint32_t mode) {
    if (!vfs_state.initialized || path == NULL)
        return -EINVAL;

    vnode_t *vnode = NULL;
    int ret = vfs_lookup(path, &vnode);
    if (ret != 0) return ret;

    vfs_vnode_unref(vnode);
    return 0;
}

int vfs_chmod(const char *path, uint32_t mode) {
    if (!vfs_state.initialized || path == NULL)
        return -EINVAL;

    vnode_t *vnode = NULL;
    int ret = vfs_lookup(path, &vnode);
    if (ret != 0) return ret;

    vnode->v_mode = mode;

    if (vnode->v_ops && vnode->v_ops->setattr) {
        vfs_stat_t stat;
        memset(&stat, 0, sizeof(stat));
        stat.st_mode = mode;
        ret = vnode->v_ops->setattr(vnode, &stat);
    }

    vfs_vnode_unref(vnode);
    return ret;
}

int vfs_chown(const char *path, uint32_t uid, uint32_t gid) {
    if (!vfs_state.initialized || path == NULL)
        return -EINVAL;

    vnode_t *vnode = NULL;
    int ret = vfs_lookup(path, &vnode);
    if (ret != 0) return ret;

    vnode->v_uid = uid;
    vnode->v_gid = gid;

    if (vnode->v_ops && vnode->v_ops->setattr) {
        vfs_stat_t stat;
        memset(&stat, 0, sizeof(stat));
        stat.st_uid = uid;
        stat.st_gid = gid;
        ret = vnode->v_ops->setattr(vnode, &stat);
    }

    vfs_vnode_unref(vnode);
    return ret;
}

void vfs_dump_mounts(void) {
    if (!vfs_state.initialized) { printk("vfs: not initialized\n"); return; }

    printk("vfs: mounted filesystems:\n");
    spinlock_irq_acquire(&vfs_state.mount_lock);

    vfs_mount_t *mount = vfs_state.mount_list;
    int count = 0;

    while (mount != NULL) {
        printk("  %s on %s type %s (%s)\n",
               mount->mnt_dev ? mount->mnt_dev->name : "none",
               mount->mnt_path,
               mount->mnt_ops->fs_name,
               (mount->mnt_flags & VFS_MNT_RDONLY) ? "ro" : "rw");
        count++;
        mount = mount->mnt_next;
    }

    spinlock_irq_release(&vfs_state.mount_lock);
    printk("vfs: total mounts: %d\n", count);
}

void vfs_dump_vnodes(void) {
    if (!vfs_state.initialized) { printk("vfs: not initialized\n"); return; }

    printk("vfs: active vnodes:\n");
    spinlock_irq_acquire(&vfs_state.vnode_lock);

    vnode_t *vnode = vfs_state.vnode_list;
    int count = 0;

    while (vnode != NULL) {
        const char *type_str = "unknown";
        switch (vnode->v_type) {
            case VFS_TYPE_FILE:      type_str = "file";   break;
            case VFS_TYPE_DIR:       type_str = "dir";    break;
            case VFS_TYPE_CHARDEV:   type_str = "char";   break;
            case VFS_TYPE_BLOCKDEV:  type_str = "block";  break;
            case VFS_TYPE_PIPE:      type_str = "pipe";   break;
            case VFS_TYPE_SYMLINK:   type_str = "link";   break;
            case VFS_TYPE_SOCKET:    type_str = "socket"; break;
        }

        printk("  vnode %lu: type=%s, size=%lu, refcount=%u\n",
               vnode->v_ino, type_str, vnode->v_size, vnode->v_refcount);
        count++;
        vnode = vnode->v_next;
    }

    spinlock_irq_release(&vfs_state.vnode_lock);
    printk("vfs: total vnodes: %d\n", count);
}

void vfs_print_stats(void) {
    if (!vfs_state.initialized) { printk("vfs: not initialized\n"); return; }

    printk("vfs statistics:\n");
    printk("  lookups:           %lu\n", vfs_state.stat_lookups);
    printk("  opens:             %lu\n", vfs_state.stat_opens);
    printk("  reads:             %lu\n", vfs_state.stat_reads);
    printk("  writes:            %lu\n", vfs_state.stat_writes);
    printk("  vnodes allocated:  %lu\n", vfs_state.stat_vnodes_allocated);
    printk("  vnodes freed:      %lu\n", vfs_state.stat_vnodes_freed);
    printk("  vnodes active:     %lu\n",
           vfs_state.stat_vnodes_allocated - vfs_state.stat_vnodes_freed);
}