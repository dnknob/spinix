#include <fs/tmpfs.h>
#include <fs/vfs.h>

#include <core/spinlock.h>

#include <mm/heap.h>

#include <video/printk.h>

#include <klibc/string.h>
#include <errno.h>

#define TMPFS_FILE_INITIAL_CAP  4096
#define TMPFS_FILE_GROW_FACTOR  2

static const vnode_ops_t tmpfs_file_ops;
static const vnode_ops_t tmpfs_dir_ops;
static const vnode_ops_t tmpfs_symlink_ops;
static const vfs_filesystem_ops_t tmpfs_fs_ops;

tmpfs_node_t *tmpfs_node_alloc(tmpfs_mount_data_t *mount_data, uint32_t type) {
    tmpfs_node_t *node = (tmpfs_node_t *)kmalloc(sizeof(tmpfs_node_t));
    if (node == NULL) {
        return NULL;
    }

    memset(node, 0, sizeof(tmpfs_node_t));
    
    spinlock_irq_acquire(&mount_data->lock);
    node->ino = mount_data->next_ino++;
    mount_data->total_nodes++;
    spinlock_irq_release(&mount_data->lock);

    node->type = type;
    spinlock_irq_init(&node->lock);

    switch (type) {
        case VFS_TYPE_FILE:
            node->data.file.data = NULL;
            node->data.file.capacity = 0;
            node->data.file.size = 0;
            break;

        case VFS_TYPE_DIR:
            node->data.dir.entries = NULL;
            node->data.dir.num_entries = 0;
            break;

        case VFS_TYPE_SYMLINK:
            node->data.symlink.target = NULL;
            node->data.symlink.length = 0;
            break;

        default:
            kfree(node);
            return NULL;
    }

    return node;
}

void tmpfs_node_free(tmpfs_node_t *node) {
    if (node == NULL) {
        return;
    }

    switch (node->type) {
        case VFS_TYPE_FILE:
            if (node->data.file.data != NULL) {
                kfree(node->data.file.data);
            }
            break;

        case VFS_TYPE_DIR: {
            tmpfs_dirent_t *entry = node->data.dir.entries;
            while (entry != NULL) {
                tmpfs_dirent_t *next = entry->next;
                kfree(entry);
                entry = next;
            }
            break;
        }

        case VFS_TYPE_SYMLINK:
            if (node->data.symlink.target != NULL) {
                kfree(node->data.symlink.target);
            }
            break;
    }

    kfree(node);
}

tmpfs_dirent_t *tmpfs_dirent_lookup(tmpfs_node_t *dir_node, const char *name) {
    if (dir_node == NULL || dir_node->type != VFS_TYPE_DIR || name == NULL) {
        return NULL;
    }

    tmpfs_dirent_t *entry = dir_node->data.dir.entries;
    while (entry != NULL) {
        if (strcmp(entry->name, name) == 0) {
            return entry;
        }
        entry = entry->next;
    }

    return NULL;
}

int tmpfs_dirent_add(tmpfs_node_t *dir_node, const char *name, vnode_t *vnode) {
    if (dir_node == NULL || dir_node->type != VFS_TYPE_DIR || 
        name == NULL || vnode == NULL) {
        return -EINVAL;
    }

    if (tmpfs_dirent_lookup(dir_node, name) != NULL) {
        return -EEXIST;
    }

    tmpfs_dirent_t *entry = (tmpfs_dirent_t *)kmalloc(sizeof(tmpfs_dirent_t));
    if (entry == NULL) {
        return -ENOMEM;
    }

    strncpy(entry->name, name, VFS_NAME_MAX);
    entry->name[VFS_NAME_MAX] = '\0';
    entry->vnode = vnode;
    entry->next = dir_node->data.dir.entries;

    dir_node->data.dir.entries = entry;
    dir_node->data.dir.num_entries++;

    vfs_vnode_ref(vnode);  /* Add reference */

    return 0;
}

int tmpfs_dirent_remove(tmpfs_node_t *dir_node, const char *name) {
    if (dir_node == NULL || dir_node->type != VFS_TYPE_DIR || name == NULL) {
        return -EINVAL;
    }

    tmpfs_dirent_t **prev = &dir_node->data.dir.entries;
    tmpfs_dirent_t *entry = dir_node->data.dir.entries;

    while (entry != NULL) {
        if (strcmp(entry->name, name) == 0) {
            *prev = entry->next;
            dir_node->data.dir.num_entries--;

            vfs_vnode_unref(entry->vnode);  /* Release reference */
            kfree(entry);

            return 0;
        }

        prev = &entry->next;
        entry = entry->next;
    }

    return -ENOENT;
}

bool tmpfs_dir_is_empty(tmpfs_node_t *dir_node) {
    if (dir_node == NULL || dir_node->type != VFS_TYPE_DIR) {
        return false;
    }

    tmpfs_dirent_t *entry = dir_node->data.dir.entries;
    while (entry != NULL) {
        if (strcmp(entry->name, ".") != 0 && strcmp(entry->name, "..") != 0) {
            return false;
        }
        entry = entry->next;
    }

    return true;
}

int tmpfs_mount(struct blk_device *dev, uint32_t flags, vfs_mount_t **mount_out) {
    (void)dev;  /* tmpfs doesn't use block devices */
    (void)flags;

    vfs_mount_t *mount = (vfs_mount_t *)kmalloc(sizeof(vfs_mount_t));
    if (mount == NULL) {
        return -ENOMEM;
    }

    memset(mount, 0, sizeof(vfs_mount_t));

    tmpfs_mount_data_t *mount_data = (tmpfs_mount_data_t *)kmalloc(sizeof(tmpfs_mount_data_t));
    if (mount_data == NULL) {
        kfree(mount);
        return -ENOMEM;
    }

    memset(mount_data, 0, sizeof(tmpfs_mount_data_t));
    mount_data->next_ino = 1;
    mount_data->total_nodes = 0;
    mount_data->total_size = 0;
    mount_data->max_size = 0;  /* Unlimited by default */
    spinlock_irq_init(&mount_data->lock);

    mount->mnt_data = mount_data;
    mount->mnt_ops = &tmpfs_fs_ops;

    vnode_t *root = vfs_vnode_alloc(mount);
    if (root == NULL) {
        kfree(mount_data);
        kfree(mount);
        return -ENOMEM;
    }

    tmpfs_node_t *root_node = tmpfs_node_alloc(mount_data, VFS_TYPE_DIR);
    if (root_node == NULL) {
        kfree(root);
        kfree(mount_data);
        kfree(mount);
        return -ENOMEM;
    }

    root->v_ino = root_node->ino;
    root->v_type = VFS_TYPE_DIR;
    root->v_mode = VFS_PERM_RUSR | VFS_PERM_WUSR | VFS_PERM_XUSR |
                   VFS_PERM_RGRP | VFS_PERM_XGRP |
                   VFS_PERM_ROTH | VFS_PERM_XOTH;  /* 0755 */
    root->v_size = 0;
    root->v_ops = &tmpfs_dir_ops;
    root->v_data = root_node;
    root->v_parent = root;  /* Root's parent is itself */

    tmpfs_dirent_add(root_node, ".", root);
    tmpfs_dirent_add(root_node, "..", root);

    mount->mnt_root = root;

    *mount_out = mount;

    printk("tmpfs: mounted successfully\n");
    return 0;
}

int tmpfs_unmount(vfs_mount_t *mount) {
    if (mount == NULL) {
        return -EINVAL;
    }

    tmpfs_mount_data_t *mount_data = (tmpfs_mount_data_t *)mount->mnt_data;
    
    if (mount->mnt_root != NULL) {
        vfs_vnode_unref(mount->mnt_root);
    }

    if (mount_data != NULL) {
        kfree(mount_data);
    }

    printk("tmpfs: unmounted (freed %lu nodes)\n", mount_data->total_nodes);
    return 0;
}

int tmpfs_read(vnode_t *vnode, void *buf, size_t len, uint64_t offset) {
    if (vnode == NULL || buf == NULL) {
        return -EINVAL;
    }

    if (vnode->v_type != VFS_TYPE_FILE) {
        return -EISDIR;
    }

    tmpfs_node_t *node = (tmpfs_node_t *)vnode->v_data;
    if (node == NULL) {
        return -EINVAL;
    }

    spinlock_irq_acquire(&node->lock);

    if (offset >= node->data.file.size) {
        spinlock_irq_release(&node->lock);
        return 0;  /* EOF */
    }

    size_t available = node->data.file.size - offset;
    size_t to_read = (len < available) ? len : available;

    if (to_read > 0 && node->data.file.data != NULL) {
        memcpy(buf, node->data.file.data + offset, to_read);
    }

    spinlock_irq_release(&node->lock);

    return (int)to_read;
}

int tmpfs_write(vnode_t *vnode, const void *buf, size_t len, uint64_t offset) {
    if (vnode == NULL || buf == NULL) {
        return -EINVAL;
    }

    if (vnode->v_type != VFS_TYPE_FILE) {
        return -EISDIR;
    }

    tmpfs_node_t *node = (tmpfs_node_t *)vnode->v_data;
    if (node == NULL) {
        return -EINVAL;
    }

    spinlock_irq_acquire(&node->lock);

    size_t new_size = offset + len;

    if (new_size > node->data.file.capacity) {
        size_t new_capacity = node->data.file.capacity;
        
        if (new_capacity == 0) {
            new_capacity = TMPFS_FILE_INITIAL_CAP;
        }

        while (new_capacity < new_size) {
            new_capacity *= TMPFS_FILE_GROW_FACTOR;
        }

        uint8_t *new_data = (uint8_t *)kmalloc(new_capacity);
        if (new_data == NULL) {
            spinlock_irq_release(&node->lock);
            return -ENOMEM;
        }

        if (node->data.file.data != NULL) {
            memcpy(new_data, node->data.file.data, node->data.file.size);
            kfree(node->data.file.data);
        }

        node->data.file.data = new_data;
        node->data.file.capacity = new_capacity;
    }

    if (offset > node->data.file.size) {
        memset(node->data.file.data + node->data.file.size, 0, 
               offset - node->data.file.size);
    }

    memcpy(node->data.file.data + offset, buf, len);

    if (new_size > node->data.file.size) {
        node->data.file.size = new_size;
        vnode->v_size = new_size;
    }

    spinlock_irq_release(&node->lock);

    return (int)len;
}

int tmpfs_truncate(vnode_t *vnode, uint64_t size) {
    if (vnode == NULL) {
        return -EINVAL;
    }

    if (vnode->v_type != VFS_TYPE_FILE) {
        return -EISDIR;
    }

    tmpfs_node_t *node = (tmpfs_node_t *)vnode->v_data;
    if (node == NULL) {
        return -EINVAL;
    }

    spinlock_irq_acquire(&node->lock);

    if (size == 0) {
        /* Free all data */
        if (node->data.file.data != NULL) {
            kfree(node->data.file.data);
            node->data.file.data = NULL;
        }
        node->data.file.capacity = 0;
        node->data.file.size = 0;
    } else if (size < node->data.file.size) {
        /* Shrinking file */
        node->data.file.size = size;
    } else if (size > node->data.file.size) {
        /* Growing file - allocate if needed */
        if (size > node->data.file.capacity) {
            size_t new_capacity = size;
            uint8_t *new_data = (uint8_t *)kmalloc(new_capacity);
            
            if (new_data == NULL) {
                spinlock_irq_release(&node->lock);
                return -ENOMEM;
            }

            if (node->data.file.data != NULL) {
                memcpy(new_data, node->data.file.data, node->data.file.size);
                kfree(node->data.file.data);
            }

            node->data.file.data = new_data;
            node->data.file.capacity = new_capacity;
        }

        memset(node->data.file.data + node->data.file.size, 0, 
               size - node->data.file.size);
        node->data.file.size = size;
    }

    vnode->v_size = node->data.file.size;

    spinlock_irq_release(&node->lock);

    return 0;
}

int tmpfs_lookup(vnode_t *dir, const char *name, vnode_t **result) {
    if (dir == NULL || name == NULL || result == NULL) {
        return -EINVAL;
    }

    if (dir->v_type != VFS_TYPE_DIR) {
        return -ENOTDIR;
    }

    tmpfs_node_t *dir_node = (tmpfs_node_t *)dir->v_data;
    if (dir_node == NULL) {
        return -EINVAL;
    }

    spinlock_irq_acquire(&dir_node->lock);

    tmpfs_dirent_t *entry = tmpfs_dirent_lookup(dir_node, name);
    if (entry == NULL) {
        spinlock_irq_release(&dir_node->lock);
        return -ENOENT;
    }

    vfs_vnode_ref(entry->vnode);
    *result = entry->vnode;

    spinlock_irq_release(&dir_node->lock);

    return 0;
}

int tmpfs_create(vnode_t *dir, const char *name, uint32_t mode, vnode_t **result) {
    if (dir == NULL || name == NULL || result == NULL) {
        return -EINVAL;
    }

    if (dir->v_type != VFS_TYPE_DIR) {
        return -ENOTDIR;
    }

    tmpfs_node_t *dir_node = (tmpfs_node_t *)dir->v_data;
    if (dir_node == NULL) {
        return -EINVAL;
    }

    tmpfs_mount_data_t *mount_data = (tmpfs_mount_data_t *)dir->v_mount->mnt_data;

    vnode_t *file_vnode = vfs_vnode_alloc(dir->v_mount);
    if (file_vnode == NULL) {
        return -ENOMEM;
    }

    tmpfs_node_t *file_node = tmpfs_node_alloc(mount_data, VFS_TYPE_FILE);
    if (file_node == NULL) {
        kfree(file_vnode);
        return -ENOMEM;
    }

    file_vnode->v_ino = file_node->ino;
    file_vnode->v_type = VFS_TYPE_FILE;
    file_vnode->v_mode = mode;
    file_vnode->v_size = 0;
    file_vnode->v_ops = &tmpfs_file_ops;
    file_vnode->v_data = file_node;
    file_vnode->v_parent = dir;

    spinlock_irq_acquire(&dir_node->lock);
    int ret = tmpfs_dirent_add(dir_node, name, file_vnode);
    spinlock_irq_release(&dir_node->lock);

    if (ret != 0) {
        tmpfs_node_free(file_node);
        kfree(file_vnode);
        return ret;
    }

    *result = file_vnode;
    return 0;
}

int tmpfs_mkdir(vnode_t *dir, const char *name, uint32_t mode, vnode_t **result) {
    if (dir == NULL || name == NULL || result == NULL) {
        return -EINVAL;
    }

    if (dir->v_type != VFS_TYPE_DIR) {
        return -ENOTDIR;
    }

    tmpfs_node_t *dir_node = (tmpfs_node_t *)dir->v_data;
    if (dir_node == NULL) {
        return -EINVAL;
    }

    tmpfs_mount_data_t *mount_data = (tmpfs_mount_data_t *)dir->v_mount->mnt_data;

    vnode_t *new_dir_vnode = vfs_vnode_alloc(dir->v_mount);
    if (new_dir_vnode == NULL) {
        return -ENOMEM;
    }

    tmpfs_node_t *new_dir_node = tmpfs_node_alloc(mount_data, VFS_TYPE_DIR);
    if (new_dir_node == NULL) {
        kfree(new_dir_vnode);
        return -ENOMEM;
    }

    new_dir_vnode->v_ino = new_dir_node->ino;
    new_dir_vnode->v_type = VFS_TYPE_DIR;
    new_dir_vnode->v_mode = mode;
    new_dir_vnode->v_size = 0;
    new_dir_vnode->v_ops = &tmpfs_dir_ops;
    new_dir_vnode->v_data = new_dir_node;
    new_dir_vnode->v_parent = dir;

    tmpfs_dirent_add(new_dir_node, ".", new_dir_vnode);
    tmpfs_dirent_add(new_dir_node, "..", dir);

    spinlock_irq_acquire(&dir_node->lock);
    int ret = tmpfs_dirent_add(dir_node, name, new_dir_vnode);
    spinlock_irq_release(&dir_node->lock);

    if (ret != 0) {
        /* Cleanup on failure */
        vfs_vnode_unref(new_dir_vnode);  /* Releases . reference */
        vfs_vnode_unref(dir);             /* Releases .. reference */
        tmpfs_node_free(new_dir_node);
        kfree(new_dir_vnode);
        return ret;
    }

    *result = new_dir_vnode;
    return 0;
}

int tmpfs_rmdir(vnode_t *dir, const char *name) {
    if (dir == NULL || name == NULL) {
        return -EINVAL;
    }

    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
        return -EINVAL;
    }

    if (dir->v_type != VFS_TYPE_DIR) {
        return -ENOTDIR;
    }

    tmpfs_node_t *dir_node = (tmpfs_node_t *)dir->v_data;
    if (dir_node == NULL) {
        return -EINVAL;
    }

    spinlock_irq_acquire(&dir_node->lock);

    tmpfs_dirent_t *entry = tmpfs_dirent_lookup(dir_node, name);
    if (entry == NULL) {
        spinlock_irq_release(&dir_node->lock);
        return -ENOENT;
    }

    vnode_t *target = entry->vnode;
    if (target->v_type != VFS_TYPE_DIR) {
        spinlock_irq_release(&dir_node->lock);
        return -ENOTDIR;
    }

    tmpfs_node_t *target_node = (tmpfs_node_t *)target->v_data;

    spinlock_irq_acquire(&target_node->lock);
    bool is_empty = tmpfs_dir_is_empty(target_node);
    spinlock_irq_release(&target_node->lock);

    if (!is_empty) {
        spinlock_irq_release(&dir_node->lock);
        return -ENOTEMPTY;
    }

    int ret = tmpfs_dirent_remove(dir_node, name);

    spinlock_irq_release(&dir_node->lock);

    return ret;
}

int tmpfs_unlink(vnode_t *dir, const char *name) {
    if (dir == NULL || name == NULL) {
        return -EINVAL;
    }

    if (dir->v_type != VFS_TYPE_DIR) {
        return -ENOTDIR;
    }

    tmpfs_node_t *dir_node = (tmpfs_node_t *)dir->v_data;
    if (dir_node == NULL) {
        return -EINVAL;
    }

    spinlock_irq_acquire(&dir_node->lock);

    tmpfs_dirent_t *entry = tmpfs_dirent_lookup(dir_node, name);
    if (entry == NULL) {
        spinlock_irq_release(&dir_node->lock);
        return -ENOENT;
    }

    vnode_t *target = entry->vnode;
    if (target->v_type == VFS_TYPE_DIR) {
        spinlock_irq_release(&dir_node->lock);
        return -EISDIR;
    }

    int ret = tmpfs_dirent_remove(dir_node, name);

    spinlock_irq_release(&dir_node->lock);

    return ret;
}

int tmpfs_rename(vnode_t *old_dir, const char *old_name,
                  vnode_t *new_dir, const char *new_name) {
    if (old_dir == NULL || old_name == NULL || 
        new_dir == NULL || new_name == NULL) {
        return -EINVAL;
    }

    tmpfs_node_t *old_dir_node = (tmpfs_node_t *)old_dir->v_data;
    tmpfs_node_t *new_dir_node = (tmpfs_node_t *)new_dir->v_data;

    if (old_dir_node == NULL || new_dir_node == NULL) {
        return -EINVAL;
    }

    tmpfs_node_t *first = (old_dir_node < new_dir_node) ? old_dir_node : new_dir_node;
    tmpfs_node_t *second = (old_dir_node < new_dir_node) ? new_dir_node : old_dir_node;

    spinlock_irq_acquire(&first->lock);
    if (first != second) {
        spinlock_irq_acquire(&second->lock);
    }

    tmpfs_dirent_t *old_entry = tmpfs_dirent_lookup(old_dir_node, old_name);
    if (old_entry == NULL) {
        if (first != second) {
            spinlock_irq_release(&second->lock);
        }
        spinlock_irq_release(&first->lock);
        return -ENOENT;
    }

    vnode_t *target = old_entry->vnode;

    tmpfs_dirent_t *new_entry = tmpfs_dirent_lookup(new_dir_node, new_name);
    if (new_entry != NULL) {
        /* Destination exists - remove it first */
        tmpfs_dirent_remove(new_dir_node, new_name);
    }

    vfs_vnode_ref(target);
    int ret = tmpfs_dirent_add(new_dir_node, new_name, target);
    
    if (ret == 0) {
        /* Remove from old location */
        tmpfs_dirent_remove(old_dir_node, old_name);
    } else {
        vfs_vnode_unref(target);
    }

    if (first != second) {
        spinlock_irq_release(&second->lock);
    }
    spinlock_irq_release(&first->lock);

    return ret;
}

int tmpfs_readdir(vnode_t *dir, vfs_dirent_t *dirent, uint64_t *offset) {
    if (dir == NULL || dirent == NULL || offset == NULL) {
        return -EINVAL;
    }

    if (dir->v_type != VFS_TYPE_DIR) {
        return -ENOTDIR;
    }

    tmpfs_node_t *dir_node = (tmpfs_node_t *)dir->v_data;
    if (dir_node == NULL) {
        return -EINVAL;
    }

    spinlock_irq_acquire(&dir_node->lock);

    tmpfs_dirent_t *entry = dir_node->data.dir.entries;
    uint64_t current = 0;

    while (entry != NULL && current < *offset) {
        entry = entry->next;
        current++;
    }

    if (entry == NULL) {
        spinlock_irq_release(&dir_node->lock);
        return 0;  /* End of directory */
    }
    
    dirent->d_ino = entry->vnode->v_ino;
    dirent->d_off = *offset + 1;
    dirent->d_reclen = sizeof(vfs_dirent_t);
    
    switch (entry->vnode->v_type) {
        case VFS_TYPE_FILE:    dirent->d_type = VFS_DT_REG; break;
        case VFS_TYPE_DIR:     dirent->d_type = VFS_DT_DIR; break;
        case VFS_TYPE_SYMLINK: dirent->d_type = VFS_DT_LNK; break;
        case VFS_TYPE_CHARDEV: dirent->d_type = VFS_DT_CHR; break;
        case VFS_TYPE_BLOCKDEV: dirent->d_type = VFS_DT_BLK; break;
        default:               dirent->d_type = VFS_DT_UNKNOWN; break;
    }

    strncpy(dirent->d_name, entry->name, VFS_NAME_MAX);
    dirent->d_name[VFS_NAME_MAX] = '\0';

    *offset = *offset + 1;

    spinlock_irq_release(&dir_node->lock);

    return 1;  /* Successfully read one entry */
}

int tmpfs_symlink(vnode_t *dir, const char *name, const char *target) {
    if (dir == NULL || name == NULL || target == NULL) {
        return -EINVAL;
    }

    if (dir->v_type != VFS_TYPE_DIR) {
        return -ENOTDIR;
    }

    tmpfs_node_t *dir_node = (tmpfs_node_t *)dir->v_data;
    tmpfs_mount_data_t *mount_data = (tmpfs_mount_data_t *)dir->v_mount->mnt_data;

    vnode_t *link_vnode = vfs_vnode_alloc(dir->v_mount);
    if (link_vnode == NULL) {
        return -ENOMEM;
    }

    tmpfs_node_t *link_node = tmpfs_node_alloc(mount_data, VFS_TYPE_SYMLINK);
    if (link_node == NULL) {
        kfree(link_vnode);
        return -ENOMEM;
    }

    size_t target_len = strlen(target);
    link_node->data.symlink.target = (char *)kmalloc(target_len + 1);
    if (link_node->data.symlink.target == NULL) {
        tmpfs_node_free(link_node);
        kfree(link_vnode);
        return -ENOMEM;
    }

    strcpy(link_node->data.symlink.target, target);
    link_node->data.symlink.length = target_len;

    link_vnode->v_ino = link_node->ino;
    link_vnode->v_type = VFS_TYPE_SYMLINK;
    link_vnode->v_mode = VFS_PERM_RUSR | VFS_PERM_WUSR | VFS_PERM_XUSR |
                         VFS_PERM_RGRP | VFS_PERM_XGRP |
                         VFS_PERM_ROTH | VFS_PERM_XOTH;
    link_vnode->v_size = target_len;
    link_vnode->v_ops = &tmpfs_symlink_ops;
    link_vnode->v_data = link_node;
    link_vnode->v_parent = dir;

    spinlock_irq_acquire(&dir_node->lock);
    int ret = tmpfs_dirent_add(dir_node, name, link_vnode);
    spinlock_irq_release(&dir_node->lock);

    if (ret != 0) {
        tmpfs_node_free(link_node);
        kfree(link_vnode);
        return ret;
    }

    return 0;
}

int tmpfs_readlink(vnode_t *vnode, char *buf, size_t bufsize) {
    if (vnode == NULL || buf == NULL) {
        return -EINVAL;
    }

    if (vnode->v_type != VFS_TYPE_SYMLINK) {
        return -EINVAL;
    }

    tmpfs_node_t *node = (tmpfs_node_t *)vnode->v_data;
    if (node == NULL || node->data.symlink.target == NULL) {
        return -EINVAL;
    }

    spinlock_irq_acquire(&node->lock);

    size_t copy_len = node->data.symlink.length;
    if (copy_len >= bufsize) {
        copy_len = bufsize - 1;
    }

    memcpy(buf, node->data.symlink.target, copy_len);
    buf[copy_len] = '\0';

    spinlock_irq_release(&node->lock);

    return (int)copy_len;
}

int tmpfs_link(vnode_t *dir, const char *name, vnode_t *target) {
    if (dir == NULL || name == NULL || target == NULL) {
        return -EINVAL;
    }

    if (dir->v_type != VFS_TYPE_DIR) {
        return -ENOTDIR;
    }

    if (target->v_type == VFS_TYPE_DIR) {
        return -EPERM;  /* Can't hard link directories */
    }

    tmpfs_node_t *dir_node = (tmpfs_node_t *)dir->v_data;
    if (dir_node == NULL) {
        return -EINVAL;
    }

    spinlock_irq_acquire(&dir_node->lock);

    int ret = tmpfs_dirent_add(dir_node, name, target);
    if (ret == 0) {
        target->v_nlink++;
    }

    spinlock_irq_release(&dir_node->lock);

    return ret;
}

int tmpfs_getattr(vnode_t *vnode, vfs_stat_t *stat) {
    if (vnode == NULL || stat == NULL) {
        return -EINVAL;
    }

    memset(stat, 0, sizeof(vfs_stat_t));

    stat->st_ino = vnode->v_ino;
    stat->st_mode = vnode->v_mode;
    stat->st_nlink = vnode->v_nlink;
    stat->st_uid = vnode->v_uid;
    stat->st_gid = vnode->v_gid;
    stat->st_size = vnode->v_size;
    stat->st_atime = vnode->v_atime;
    stat->st_mtime = vnode->v_mtime;
    stat->st_ctime = vnode->v_ctime;
    stat->st_blksize = 4096;
    stat->st_blocks = (vnode->v_size + 511) / 512;

    return 0;
}

int tmpfs_get_stats(const char *mountpoint, tmpfs_stats_t *out) {
    if (out == NULL) return -EINVAL;

    vfs_mount_t *mount = vfs_find_mount(mountpoint);
    if (mount == NULL) return -ENOENT;

    tmpfs_mount_data_t *md = (tmpfs_mount_data_t *)mount->mnt_data;
    if (md == NULL) return -EINVAL;

    spinlock_irq_acquire(&md->lock);
    out->total_nodes = md->total_nodes;
    out->total_size  = md->total_size;
    out->max_size    = md->max_size;
    spinlock_irq_release(&md->lock);

    return 0;
}

void tmpfs_release(vnode_t *vnode) {
    if (vnode == NULL || vnode->v_data == NULL) {
        return;
    }

    tmpfs_node_t *node = (tmpfs_node_t *)vnode->v_data;
    tmpfs_node_free(node);
    vnode->v_data = NULL;
}

static const vnode_ops_t tmpfs_file_ops = {
    .read = tmpfs_read,
    .write = tmpfs_write,
    .truncate = tmpfs_truncate,
    .getattr = tmpfs_getattr,
    .release = tmpfs_release,
};

static const vnode_ops_t tmpfs_dir_ops = {
    .lookup = tmpfs_lookup,
    .create = tmpfs_create,
    .mkdir = tmpfs_mkdir,
    .rmdir = tmpfs_rmdir,
    .unlink = tmpfs_unlink,
    .rename = tmpfs_rename,
    .readdir = tmpfs_readdir,
    .symlink = tmpfs_symlink,
    .link = tmpfs_link,
    .getattr = tmpfs_getattr,
    .release = tmpfs_release,
};

static const vnode_ops_t tmpfs_symlink_ops = {
    .readlink = tmpfs_readlink,
    .getattr = tmpfs_getattr,
    .release = tmpfs_release,
};

static const vfs_filesystem_ops_t tmpfs_fs_ops = {
    .fs_name = "tmpfs",
    .mount = tmpfs_mount,
    .unmount = tmpfs_unmount,
    .alloc_vnode = NULL,  /* Use VFS default */
    .free_vnode = NULL,   /* Use VFS default */
};

int tmpfs_init(void) {
    int ret = vfs_register_filesystem(&tmpfs_fs_ops);
    if (ret != 0) {
        printk("tmpfs: failed to register filesystem: %d\n", ret);
        return ret;
    }

    printk("tmpfs: registered filesystem type\n");
    return 0;
}
