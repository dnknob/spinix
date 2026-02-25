#ifndef _SYSFS_H
#define _SYSFS_H

#include <klibc/types.h>

#include <core/spinlock.h>

#include <fs/vfs.h>

typedef int (*sysfs_show_t)(char *buf, size_t size);
typedef int (*sysfs_store_t)(const char *buf, size_t len);

typedef struct sysfs_attr {
    const char   *name;     /* filename under the parent dir            */
    mode_t        mode;     /* permission bits (e.g. 0444, 0644)        */
    sysfs_show_t  show;     /* read  handler — NULL => file unreadable  */
    sysfs_store_t store;    /* write handler — NULL => file read-only   */
} sysfs_attr_t;

#define SYSFS_ATTR_RO(_name, _show) \
    { .name = (_name), .mode = 0444, .show = (_show), .store = NULL }

#define SYSFS_ATTR_RW(_name, _show, _store) \
    { .name = (_name), .mode = 0644, .show = (_show), .store = (_store) }

#define SYSFS_ATTR_WO(_name, _store) \
    { .name = (_name), .mode = 0200, .show = NULL, .store = (_store) }

#define SYSFS_NODE_DIR   1   /* directory  */
#define SYSFS_NODE_ATTR  2   /* attribute file */

typedef struct sysfs_node {
    char     name[VFS_NAME_MAX + 1]; /* entry name                      */
    uint32_t type;                   /* SYSFS_NODE_DIR / SYSFS_NODE_ATTR */
    uint64_t ino;                    /* inode number                     */

    sysfs_show_t  show;
    sysfs_store_t store;

    struct sysfs_node *parent;       /* parent dir node                  */
    struct sysfs_node *children;     /* first child (dirs only)          */
    struct sysfs_node *next;         /* next sibling                     */

    vnode_t *vnode;

    spinlock_irq_t lock;             /* protects children / next         */
} sysfs_node_t;

typedef struct sysfs_mount_data {
    sysfs_node_t  *root_node;        /* root sysfs_node_t                */
    uint64_t       next_ino;         /* inode counter                    */
    spinlock_irq_t lock;             /* global mount lock                */
} sysfs_mount_data_t;

int sysfs_init(void);

int sysfs_mkdir(const char *path, uint32_t mode);
int sysfs_rmdir(const char *path);

int sysfs_create_attr(const char *dir_path, const sysfs_attr_t *attr);
int sysfs_remove_attr(const char *dir_path, const char *attr_name);

int sysfs_mount(struct blk_device *dev, uint32_t flags, vfs_mount_t **mount_out);
int sysfs_unmount(vfs_mount_t *mount);

int sysfs_read(vnode_t *vnode, void *buf, size_t len, uint64_t offset);
int sysfs_write(vnode_t *vnode, const void *buf, size_t len, uint64_t offset);
int sysfs_lookup(vnode_t *dir, const char *name, vnode_t **result);
int sysfs_readdir(vnode_t *dir, vfs_dirent_t *dirent, uint64_t *offset);
int sysfs_getattr(vnode_t *vnode, vfs_stat_t *stat);
void sysfs_release(vnode_t *vnode);

#endif
