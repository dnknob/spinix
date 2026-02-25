#ifndef _TMPFS_H
#define _TMPFS_H

#include <klibc/types.h>

#include <core/spinlock.h>

#include <fs/vfs.h>

struct tmpfs_node;
struct tmpfs_dirent;
struct tmpfs_mount_data;

typedef struct tmpfs_dirent {
    char name[VFS_NAME_MAX + 1];    /* Entry name */
    struct vnode *vnode;             /* Target vnode */
    struct tmpfs_dirent *next;       /* Next entry in directory */
} tmpfs_dirent_t;

typedef struct tmpfs_node {
    ino_t    ino;                    /* Inode number */
    uint32_t type;                   /* Node type (file, dir, symlink) */
    
    union {
        /* For regular files */
        struct {
            uint8_t *data;           /* File data buffer */
            size_t capacity;         /* Allocated capacity */
            size_t size;             /* Actual data size */
        } file;
        
        struct {
            tmpfs_dirent_t *entries; /* Directory entries */
            uint32_t num_entries;    /* Number of entries */
        } dir;
        
        struct {
            char *target;            /* Symlink target path */
            size_t length;           /* Target path length */
        } symlink;
    } data;
    
    spinlock_irq_t lock;             /* Protects this node */
} tmpfs_node_t;

typedef struct tmpfs_mount_data {
    uint64_t next_ino;               /* Next inode number to allocate */
    uint64_t total_nodes;            /* Total nodes created */
    uint64_t total_size;             /* Total bytes used */
    uint64_t max_size;               /* Maximum size (0 = unlimited) */
    spinlock_irq_t lock;             /* Protects mount data */
} tmpfs_mount_data_t;

typedef struct tmpfs_stats {
    uint64_t total_nodes;
    uint64_t total_size;
    uint64_t max_size;
} tmpfs_stats_t;

int tmpfs_init(void);

int tmpfs_mount(struct blk_device *dev, uint32_t flags, vfs_mount_t **mount);
int tmpfs_unmount(vfs_mount_t *mount);
int tmpfs_alloc_vnode(vfs_mount_t *mount, vnode_t **vnode);
void tmpfs_free_vnode(vnode_t *vnode);

int tmpfs_read(vnode_t *vnode, void *buf, size_t len, uint64_t offset);
int tmpfs_write(vnode_t *vnode, const void *buf, size_t len, uint64_t offset);
int tmpfs_truncate(vnode_t *vnode, uint64_t size);
int tmpfs_lookup(vnode_t *dir, const char *name, vnode_t **result);
int tmpfs_create(vnode_t *dir, const char *name, uint32_t mode, vnode_t **result);
int tmpfs_mkdir(vnode_t *dir, const char *name, uint32_t mode, vnode_t **result);
int tmpfs_rmdir(vnode_t *dir, const char *name);
int tmpfs_unlink(vnode_t *dir, const char *name);
int tmpfs_rename(vnode_t *old_dir, const char *old_name, 
                  vnode_t *new_dir, const char *new_name);
int tmpfs_readdir(vnode_t *dir, vfs_dirent_t *dirent, uint64_t *offset);
int tmpfs_symlink(vnode_t *dir, const char *name, const char *target);
int tmpfs_readlink(vnode_t *vnode, char *buf, size_t bufsize);
int tmpfs_link(vnode_t *dir, const char *name, vnode_t *target);
int tmpfs_getattr(vnode_t *vnode, vfs_stat_t *stat);
void tmpfs_release(vnode_t *vnode);

tmpfs_node_t *tmpfs_node_alloc(tmpfs_mount_data_t *mount_data, uint32_t type);
void tmpfs_node_free(tmpfs_node_t *node);
tmpfs_dirent_t *tmpfs_dirent_lookup(tmpfs_node_t *dir_node, const char *name);
int tmpfs_dirent_add(tmpfs_node_t *dir_node, const char *name, vnode_t *vnode);
int tmpfs_dirent_remove(tmpfs_node_t *dir_node, const char *name);
bool tmpfs_dir_is_empty(tmpfs_node_t *dir_node);

int tmpfs_get_stats(const char *mountpoint, tmpfs_stats_t *out);

#endif
