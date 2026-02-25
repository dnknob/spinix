#include <core/spinlock.h>

#include <fs/sysfs.h>
#include <fs/vfs.h>

#include <mm/heap.h>

#include <video/printk.h>

#include <klibc/string.h>
#include <errno.h>

#define SYSFS_PAGE_SIZE  4096

static const vnode_ops_t sysfs_dir_ops;
static const vnode_ops_t sysfs_attr_ops;
static const vfs_filesystem_ops_t sysfs_fs_ops;

static vfs_mount_t *g_sysfs_mount = NULL;

static uint64_t sysfs_next_ino(sysfs_mount_data_t *md) {
    spinlock_irq_acquire(&md->lock);
    uint64_t ino = md->next_ino++;
    spinlock_irq_release(&md->lock);
    return ino;
}

static sysfs_node_t *sysfs_node_alloc(sysfs_mount_data_t *md,
                                       const char *name, uint32_t type) {
    if (name == NULL || strlen(name) > VFS_NAME_MAX)
        return NULL;

    sysfs_node_t *node = (sysfs_node_t *)kmalloc(sizeof(sysfs_node_t));
    if (node == NULL)
        return NULL;

    memset(node, 0, sizeof(sysfs_node_t));

    strncpy(node->name, name, VFS_NAME_MAX);
    node->name[VFS_NAME_MAX] = '\0';
    node->type      = type;
    node->ino       = sysfs_next_ino(md);
    node->children  = NULL;
    node->next      = NULL;
    node->parent    = NULL;
    node->vnode     = NULL;

    spinlock_irq_init(&node->lock);

    return node;
}

static void sysfs_node_free(sysfs_node_t *node) {
    if (node == NULL)
        return;
    kfree(node);
}

static sysfs_node_t *sysfs_child_lookup(sysfs_node_t *dir_node,
                                         const char *name) {
    sysfs_node_t *child = dir_node->children;
    while (child != NULL) {
        if (strcmp(child->name, name) == 0)
            return child;
        child = child->next;
    }
    return NULL;
}

static int sysfs_child_add(sysfs_node_t *dir_node, sysfs_node_t *child) {
    spinlock_irq_acquire(&dir_node->lock);

    if (sysfs_child_lookup(dir_node, child->name) != NULL) {
        spinlock_irq_release(&dir_node->lock);
        return -EEXIST;
    }

    child->parent      = dir_node;
    child->next        = dir_node->children;
    dir_node->children = child;

    spinlock_irq_release(&dir_node->lock);
    return 0;
}

static int sysfs_child_remove(sysfs_node_t *dir_node, const char *name) {
    spinlock_irq_acquire(&dir_node->lock);

    sysfs_node_t **pp    = &dir_node->children;
    sysfs_node_t  *child = dir_node->children;

    while (child != NULL) {
        if (strcmp(child->name, name) == 0) {
            *pp = child->next;
            spinlock_irq_release(&dir_node->lock);
            sysfs_node_free(child);
            return 0;
        }
        pp    = &child->next;
        child = child->next;
    }

    spinlock_irq_release(&dir_node->lock);
    return -ENOENT;
}

static sysfs_node_t *sysfs_path_resolve(const char *path) {
    if (g_sysfs_mount == NULL)
        return NULL;

    sysfs_mount_data_t *md = (sysfs_mount_data_t *)g_sysfs_mount->mnt_data;
    sysfs_node_t       *cur = md->root_node;

    const char *p = path;
    if (strncmp(p, "/sys", 4) == 0)
        p += 4;

    while (*p == '/')
        p++;

    if (*p == '\0')
        return cur;  /* caller wants /sys itself */

    char component[VFS_NAME_MAX + 1];

    while (*p != '\0') {
        /* Extract next component */
        size_t len = 0;
        while (p[len] != '\0' && p[len] != '/')
            len++;

        if (len == 0 || len > VFS_NAME_MAX)
            return NULL;

        memcpy(component, p, len);
        component[len] = '\0';
        p += len;
        while (*p == '/')
            p++;

        if (cur->type != SYSFS_NODE_DIR)
            return NULL;

        spinlock_irq_acquire(&cur->lock);
        sysfs_node_t *next = sysfs_child_lookup(cur, component);
        spinlock_irq_release(&cur->lock);

        if (next == NULL)
            return NULL;

        cur = next;
    }

    return cur;
}

static vnode_t *sysfs_make_vnode(vfs_mount_t *mount, sysfs_node_t *node) {
    vnode_t *vnode = vfs_vnode_alloc(mount);
    if (vnode == NULL)
        return NULL;

    vnode->v_ino  = node->ino;
    vnode->v_data = node;
    node->vnode   = vnode;

    if (node->type == SYSFS_NODE_DIR) {
        vnode->v_type = VFS_TYPE_DIR;
        vnode->v_mode = VFS_PERM_RUSR | VFS_PERM_XUSR |
                        VFS_PERM_RGRP | VFS_PERM_XGRP |
                        VFS_PERM_ROTH | VFS_PERM_XOTH;  /* 0555 */
        vnode->v_ops  = &sysfs_dir_ops;
    } else {
        vnode->v_type = VFS_TYPE_FILE;
        /* Permission comes from the attribute's .mode field.
         * We stored it in node->show != NULL => readable,
         * node->store != NULL => writable.
         * Use a conservative default; callers may adjust v_mode after. */
        uint32_t mode = 0;
        if (node->show  != NULL) mode |= VFS_PERM_RUSR | VFS_PERM_RGRP | VFS_PERM_ROTH;
        if (node->store != NULL) mode |= VFS_PERM_WUSR;
        vnode->v_mode = mode;
        vnode->v_size = SYSFS_PAGE_SIZE;  /* nominal; real size known on read */
        vnode->v_ops  = &sysfs_attr_ops;
    }

    vnode->v_nlink  = 1;
    vnode->v_parent = NULL;  /* filled by caller if needed */

    return vnode;
}

int sysfs_read(vnode_t *vnode, void *buf, size_t len, uint64_t offset) {
    if (vnode == NULL || buf == NULL)
        return -EINVAL;

    sysfs_node_t *node = (sysfs_node_t *)vnode->v_data;
    if (node == NULL || node->type != SYSFS_NODE_ATTR)
        return -EINVAL;

    if (node->show == NULL)
        return -EACCES;

    char *tmp = (char *)kmalloc(SYSFS_PAGE_SIZE);
    if (tmp == NULL)
        return -ENOMEM;

    int total = node->show(tmp, SYSFS_PAGE_SIZE);
    if (total < 0) {
        kfree(tmp);
        return total;
    }

    if (offset >= (uint64_t)total) {
        kfree(tmp);
        return 0;  /* EOF */
    }

    size_t available = (size_t)(total - offset);
    size_t to_copy   = (len < available) ? len : available;

    memcpy(buf, tmp + offset, to_copy);
    kfree(tmp);

    return (int)to_copy;
}

int sysfs_write(vnode_t *vnode, const void *buf, size_t len, uint64_t offset) {
    (void)offset;

    if (vnode == NULL || buf == NULL)
        return -EINVAL;

    sysfs_node_t *node = (sysfs_node_t *)vnode->v_data;
    if (node == NULL || node->type != SYSFS_NODE_ATTR)
        return -EINVAL;

    if (node->store == NULL)
        return -EACCES;

    return node->store((const char *)buf, len);
}

int sysfs_lookup(vnode_t *dir, const char *name, vnode_t **result) {
    if (dir == NULL || name == NULL || result == NULL)
        return -EINVAL;

    if (dir->v_type != VFS_TYPE_DIR)
        return -ENOTDIR;

    sysfs_node_t *dir_node = (sysfs_node_t *)dir->v_data;
    if (dir_node == NULL)
        return -EINVAL;

    if (strcmp(name, ".") == 0) {
        vfs_vnode_ref(dir);
        *result = dir;
        return 0;
    }

    if (strcmp(name, "..") == 0) {
        vnode_t *parent = (dir->v_parent != NULL) ? dir->v_parent : dir;
        vfs_vnode_ref(parent);
        *result = parent;
        return 0;
    }

    spinlock_irq_acquire(&dir_node->lock);
    sysfs_node_t *child = sysfs_child_lookup(dir_node, name);
    spinlock_irq_release(&dir_node->lock);

    if (child == NULL)
        return -ENOENT;

    if (child->vnode != NULL) {
        vfs_vnode_ref(child->vnode);
        *result = child->vnode;
        return 0;
    }

    vnode_t *vnode = sysfs_make_vnode(dir->v_mount, child);
    if (vnode == NULL)
        return -ENOMEM;

    vnode->v_parent = dir;
    *result = vnode;
    return 0;
}

int sysfs_readdir(vnode_t *dir, vfs_dirent_t *dirent, uint64_t *offset) {
    if (dir == NULL || dirent == NULL || offset == NULL)
        return -EINVAL;

    if (dir->v_type != VFS_TYPE_DIR)
        return -ENOTDIR;

    sysfs_node_t *dir_node = (sysfs_node_t *)dir->v_data;
    if (dir_node == NULL)
        return -EINVAL;

    spinlock_irq_acquire(&dir_node->lock);

    uint64_t pos = *offset;

    if (pos == 0) {
        /* "." */
        dirent->d_ino    = dir->v_ino;
        dirent->d_off    = 1;
        dirent->d_reclen = sizeof(vfs_dirent_t);
        dirent->d_type   = VFS_DT_DIR;
        strcpy(dirent->d_name, ".");
        *offset = 1;
        spinlock_irq_release(&dir_node->lock);
        return 1;
    }

    if (pos == 1) {
        /* ".." */
        uint64_t parent_ino = (dir->v_parent != NULL) ? dir->v_parent->v_ino
                                                       : dir->v_ino;
        dirent->d_ino    = parent_ino;
        dirent->d_off    = 2;
        dirent->d_reclen = sizeof(vfs_dirent_t);
        dirent->d_type   = VFS_DT_DIR;
        strcpy(dirent->d_name, "..");
        *offset = 2;
        spinlock_irq_release(&dir_node->lock);
        return 1;
    }

    uint64_t idx   = 2;
    sysfs_node_t *child = dir_node->children;

    while (child != NULL && idx < pos) {
        child = child->next;
        idx++;
    }

    if (child == NULL) {
        spinlock_irq_release(&dir_node->lock);
        return 0;  /* end of directory */
    }

    dirent->d_ino    = child->ino;
    dirent->d_off    = pos + 1;
    dirent->d_reclen = sizeof(vfs_dirent_t);
    dirent->d_type   = (child->type == SYSFS_NODE_DIR) ? VFS_DT_DIR
                                                        : VFS_DT_REG;

    strncpy(dirent->d_name, child->name, VFS_NAME_MAX);
    dirent->d_name[VFS_NAME_MAX] = '\0';

    *offset = pos + 1;

    spinlock_irq_release(&dir_node->lock);
    return 1;
}

int sysfs_getattr(vnode_t *vnode, vfs_stat_t *stat) {
    if (vnode == NULL || stat == NULL)
        return -EINVAL;

    memset(stat, 0, sizeof(vfs_stat_t));

    stat->st_ino     = vnode->v_ino;
    stat->st_mode    = vnode->v_mode;
    stat->st_nlink   = 1;
    stat->st_uid     = 0;
    stat->st_gid     = 0;
    stat->st_size    = vnode->v_size;
    stat->st_blksize = SYSFS_PAGE_SIZE;
    stat->st_blocks  = 0;

    return 0;
}

void sysfs_release(vnode_t *vnode) {
    if (vnode == NULL)
        return;

    sysfs_node_t *node = (sysfs_node_t *)vnode->v_data;
    if (node != NULL)
        node->vnode = NULL;
}

static const vnode_ops_t sysfs_attr_ops = {
    .read    = sysfs_read,
    .write   = sysfs_write,
    .getattr = sysfs_getattr,
    .release = sysfs_release,
};

static const vnode_ops_t sysfs_dir_ops = {
    .lookup  = sysfs_lookup,
    .readdir = sysfs_readdir,
    .getattr = sysfs_getattr,
    .release = sysfs_release,
};

int sysfs_mount(struct blk_device *dev, uint32_t flags, vfs_mount_t **mount_out) {
    (void)dev;
    (void)flags;

    vfs_mount_t *mount = (vfs_mount_t *)kmalloc(sizeof(vfs_mount_t));
    if (mount == NULL)
        return -ENOMEM;

    memset(mount, 0, sizeof(vfs_mount_t));

    sysfs_mount_data_t *md = (sysfs_mount_data_t *)kmalloc(sizeof(sysfs_mount_data_t));
    if (md == NULL) {
        kfree(mount);
        return -ENOMEM;
    }

    memset(md, 0, sizeof(sysfs_mount_data_t));
    md->next_ino = 1;
    spinlock_irq_init(&md->lock);

    sysfs_node_t *root_node = sysfs_node_alloc(md, "/", SYSFS_NODE_DIR);
    if (root_node == NULL) {
        kfree(md);
        kfree(mount);
        return -ENOMEM;
    }

    md->root_node    = root_node;
    mount->mnt_data  = md;
    mount->mnt_ops   = &sysfs_fs_ops;

    vnode_t *root_vnode = vfs_vnode_alloc(mount);
    if (root_vnode == NULL) {
        sysfs_node_free(root_node);
        kfree(md);
        kfree(mount);
        return -ENOMEM;
    }

    root_vnode->v_ino    = root_node->ino;
    root_vnode->v_type   = VFS_TYPE_DIR;
    root_vnode->v_mode   = VFS_PERM_RUSR | VFS_PERM_XUSR |
                           VFS_PERM_RGRP | VFS_PERM_XGRP |
                           VFS_PERM_ROTH | VFS_PERM_XOTH;  /* 0555 */
    root_vnode->v_nlink  = 2;
    root_vnode->v_ops    = &sysfs_dir_ops;
    root_vnode->v_data   = root_node;
    root_vnode->v_parent = root_vnode;  /* root's parent is itself */

    root_node->vnode  = root_vnode;
    mount->mnt_root   = root_vnode;

    g_sysfs_mount = mount;

    *mount_out = mount;

    printk("sysfs: mounted\n");
    return 0;
}

int sysfs_unmount(vfs_mount_t *mount) {
    if (mount == NULL)
        return -EINVAL;

    sysfs_mount_data_t *md = (sysfs_mount_data_t *)mount->mnt_data;

    if (mount->mnt_root != NULL)
        vfs_vnode_unref(mount->mnt_root);

    if (md != NULL) {
        sysfs_node_free(md->root_node);
        kfree(md);
    }

    g_sysfs_mount = NULL;

    printk("sysfs: unmounted\n");
    return 0;
}

static const vfs_filesystem_ops_t sysfs_fs_ops = {
    .fs_name    = "sysfs",
    .mount      = sysfs_mount,
    .unmount    = sysfs_unmount,
    .alloc_vnode = NULL,
    .free_vnode  = NULL,
};

int sysfs_init(void) {
    int ret = vfs_register_filesystem(&sysfs_fs_ops);
    if (ret != 0) {
        printk("sysfs: failed to register: %d\n", ret);
        return ret;
    }

    ret = vfs_mkdir("/sys", 0555);
    if (ret != 0 && ret != -EEXIST) {
        printk("sysfs: failed to create /sys mountpoint: %d\n", ret);
        return ret;
    }

    ret = vfs_mount(NULL, "/sys", "sysfs", VFS_MNT_RDONLY);
    if (ret != 0) {
        printk("sysfs: failed to mount: %d\n", ret);
        return ret;
    }

    const char *std_dirs[] = {
        "/sys/kernel",
        "/sys/devices",
        "/sys/bus",
        "/sys/class",
        "/sys/fs",
        NULL
    };

    for (int i = 0; std_dirs[i] != NULL; i++) {
        ret = sysfs_mkdir(std_dirs[i], 0555);
        if (ret != 0) {
            printk("sysfs: failed to create %s: %d\n", std_dirs[i], ret);
            /* Non-fatal; continue */
        }
    }

    printk("sysfs: initialized\n");
    return 0;
}

int sysfs_mkdir(const char *path, uint32_t mode) {
    (void)mode;  /* directories are always 0555 in sysfs */

    if (path == NULL)
        return -EINVAL;

    if (g_sysfs_mount == NULL)
        return -ENODEV;

    sysfs_mount_data_t *md = (sysfs_mount_data_t *)g_sysfs_mount->mnt_data;

    char parent_path[VFS_PATH_MAX];
    char child_name[VFS_NAME_MAX + 1];

    int ret = vfs_path_split(path, parent_path, child_name);
    if (ret != 0)
        return ret;

    if (child_name[0] == '\0')
        return -EINVAL;

    sysfs_node_t *parent_node = sysfs_path_resolve(parent_path);
    if (parent_node == NULL)
        return -ENOENT;

    if (parent_node->type != SYSFS_NODE_DIR)
        return -ENOTDIR;

    sysfs_node_t *new_node = sysfs_node_alloc(md, child_name, SYSFS_NODE_DIR);
    if (new_node == NULL)
        return -ENOMEM;

    ret = sysfs_child_add(parent_node, new_node);
    if (ret != 0) {
        sysfs_node_free(new_node);
        return ret;
    }

    return 0;
}

int sysfs_rmdir(const char *path) {
    if (path == NULL)
        return -EINVAL;

    if (g_sysfs_mount == NULL)
        return -ENODEV;

    sysfs_node_t *node = sysfs_path_resolve(path);
    if (node == NULL)
        return -ENOENT;

    if (node->type != SYSFS_NODE_DIR)
        return -ENOTDIR;

    spinlock_irq_acquire(&node->lock);
    bool has_children = (node->children != NULL);
    spinlock_irq_release(&node->lock);

    if (has_children)
        return -ENOTEMPTY;

    sysfs_node_t *parent = node->parent;
    if (parent == NULL)
        return -EBUSY;  /* cannot remove root */

    return sysfs_child_remove(parent, node->name);
}

int sysfs_create_attr(const char *dir_path, const sysfs_attr_t *attr) {
    if (dir_path == NULL || attr == NULL || attr->name == NULL)
        return -EINVAL;

    if (g_sysfs_mount == NULL)
        return -ENODEV;

    sysfs_mount_data_t *md = (sysfs_mount_data_t *)g_sysfs_mount->mnt_data;

    sysfs_node_t *dir_node = sysfs_path_resolve(dir_path);
    if (dir_node == NULL)
        return -ENOENT;

    if (dir_node->type != SYSFS_NODE_DIR)
        return -ENOTDIR;

    sysfs_node_t *attr_node = sysfs_node_alloc(md, attr->name, SYSFS_NODE_ATTR);
    if (attr_node == NULL)
        return -ENOMEM;

    attr_node->show  = attr->show;
    attr_node->store = attr->store;

    int ret = sysfs_child_add(dir_node, attr_node);
    if (ret != 0) {
        sysfs_node_free(attr_node);
        return ret;
    }

    return 0;
}

int sysfs_remove_attr(const char *dir_path, const char *attr_name) {
    if (dir_path == NULL || attr_name == NULL)
        return -EINVAL;

    if (g_sysfs_mount == NULL)
        return -ENODEV;

    sysfs_node_t *dir_node = sysfs_path_resolve(dir_path);
    if (dir_node == NULL)
        return -ENOENT;

    if (dir_node->type != SYSFS_NODE_DIR)
        return -ENOTDIR;

    spinlock_irq_acquire(&dir_node->lock);
    sysfs_node_t *target = sysfs_child_lookup(dir_node, attr_name);
    spinlock_irq_release(&dir_node->lock);

    if (target == NULL)
        return -ENOENT;

    if (target->type != SYSFS_NODE_ATTR)
        return -EISDIR;

    return sysfs_child_remove(dir_node, attr_name);
}
