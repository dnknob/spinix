#ifndef _VFS_H
#define _VFS_H

#include <klibc/types.h>

struct vnode;
struct vfs_mount;
struct vfs_file;
struct vfs_dirent;
struct blk_device;

#define VFS_TYPE_FILE           1
#define VFS_TYPE_DIR            2
#define VFS_TYPE_CHARDEV        3
#define VFS_TYPE_BLOCKDEV       4
#define VFS_TYPE_PIPE           5
#define VFS_TYPE_SYMLINK        6
#define VFS_TYPE_SOCKET         7

#define VFS_O_RDONLY            0x0000
#define VFS_O_WRONLY            0x0001
#define VFS_O_RDWR              0x0002
#define VFS_O_ACCMODE           0x0003
#define VFS_O_CREAT             0x0040
#define VFS_O_EXCL              0x0080
#define VFS_O_TRUNC             0x0200
#define VFS_O_APPEND            0x0400
#define VFS_O_NONBLOCK          0x0800
#define VFS_O_DIRECTORY         0x10000
#define VFS_O_NOFOLLOW          0x20000
#define VFS_O_CLOEXEC           0x80000

#define VFS_SEEK_SET            0
#define VFS_SEEK_CUR            1
#define VFS_SEEK_END            2

#define VFS_PERM_XUSR           0x0040  /* User execute */
#define VFS_PERM_WUSR           0x0080  /* User write */
#define VFS_PERM_RUSR           0x0100  /* User read */
#define VFS_PERM_XGRP           0x0008  /* Group execute */
#define VFS_PERM_WGRP           0x0010  /* Group write */
#define VFS_PERM_RGRP           0x0020  /* Group read */
#define VFS_PERM_XOTH           0x0001  /* Other execute */
#define VFS_PERM_WOTH           0x0002  /* Other write */
#define VFS_PERM_ROTH           0x0004  /* Other read */

#define VFS_MNT_RDONLY          (1 << 0)  /* Read-only mount */
#define VFS_MNT_NOEXEC          (1 << 1)  /* No execution from this mount */
#define VFS_MNT_NOSUID          (1 << 2)  /* Ignore suid/sgid bits */
#define VFS_MNT_NODEV           (1 << 3)  /* No device files */
#define VFS_MNT_SYNC            (1 << 4)  /* Synchronous writes */

#define VFS_DT_UNKNOWN          0
#define VFS_DT_REG              1   /* Regular file */
#define VFS_DT_DIR              2   /* Directory */
#define VFS_DT_CHR              3   /* Character device */
#define VFS_DT_BLK              4   /* Block device */
#define VFS_DT_FIFO             5   /* FIFO/pipe */
#define VFS_DT_SOCK             6   /* Socket */
#define VFS_DT_LNK              7   /* Symbolic link */

#define VFS_NAME_MAX            255
#define VFS_PATH_MAX            4096

typedef struct vfs_stat {
    dev_t    st_dev;                /* Device ID */
    ino_t    st_ino;                /* Inode number */
    mode_t   st_mode;               /* File type and permissions */
    nlink_t  st_nlink;              /* Number of hard links */
    uid_t    st_uid;                /* User ID */
    gid_t    st_gid;                /* Group ID */
    dev_t    st_rdev;               /* Device ID (if special file) */
    off_t    st_size;               /* Total size in bytes */
    uint64_t st_blksize;            /* Block size for filesystem I/O */
    uint64_t st_blocks;             /* Number of 512B blocks allocated */
    time_t   st_atime;              /* Access time (seconds since epoch) */
    time_t   st_mtime;              /* Modification time */
    time_t   st_ctime;              /* Change time */
} vfs_stat_t;

typedef struct vfs_dirent {
    uint64_t d_ino;                 /* Inode number */
    uint64_t d_off;                 /* Offset to next dirent */
    uint16_t d_reclen;              /* Length of this dirent */
    uint8_t d_type;                 /* File type (DT_*) */
    char d_name[VFS_NAME_MAX + 1];  /* Null-terminated filename */
} vfs_dirent_t;

struct vnode_ops;
struct vfs_filesystem_ops;

typedef struct vnode {
    ino_t    v_ino;                 /* Inode number */
    uint32_t v_type;                /* Node type (VFS_TYPE_*) */
    mode_t   v_mode;                /* Permissions */
    uid_t    v_uid;                 /* Owner user ID */
    gid_t    v_gid;                 /* Owner group ID */
    off_t    v_size;                /* File size in bytes */
    nlink_t  v_nlink;               /* Number of hard links */
    uint32_t v_refcount;            /* Reference count */
    
    time_t   v_atime;               /* Access time */
    time_t   v_mtime;               /* Modification time */
    time_t   v_ctime;               /* Change time */
    
    struct vfs_mount *v_mount;      /* Mount point this vnode belongs to */
    const struct vnode_ops *v_ops;  /* Operations for this vnode */
    
    void *v_data;                   /* Filesystem-specific data */
    
    struct vnode *v_parent;         /* Parent directory (for .. lookup) */
    
    uint16_t v_major;               /* Major device number */
    uint16_t v_minor;               /* Minor device number */
    
    struct vnode *v_next;           /* Next in global vnode list */
} vnode_t;

typedef struct vfs_mount {
    char mnt_path[VFS_PATH_MAX];    /* Mount point path */
    uint32_t mnt_flags;             /* Mount flags (VFS_MNT_*) */
    
    vnode_t *mnt_root;              /* Root vnode of this mount */
    vnode_t *mnt_covered;           /* Vnode this is mounted on (NULL for root) */
    
    struct blk_device *mnt_dev;     /* Block device (NULL for pseudo-filesystems) */
    const struct vfs_filesystem_ops *mnt_ops;  /* Filesystem operations */
    
    void *mnt_data;                 /* Filesystem-specific mount data */
    
    uint32_t mnt_refcount;          /* Reference count */
    struct vfs_mount *mnt_next;     /* Next mount in global list */
} vfs_mount_t;

typedef struct vfs_file {
    vnode_t *f_vnode;               /* VFS node */
    uint32_t f_flags;               /* Open flags (VFS_O_*) */
    uint64_t f_offset;              /* Current file position */
    uint32_t f_refcount;            /* Reference count */
    void *f_private;                /* Filesystem-private data */
} vfs_file_t;

typedef struct vnode_ops {
    /* File operations */
    int (*read)(vnode_t *vnode, void *buf, size_t len, uint64_t offset);
    int (*write)(vnode_t *vnode, const void *buf, size_t len, uint64_t offset);
    int (*truncate)(vnode_t *vnode, uint64_t size);
    int (*sync)(vnode_t *vnode);
    
    int (*lookup)(vnode_t *dir, const char *name, vnode_t **result);
    int (*create)(vnode_t *dir, const char *name, uint32_t mode, vnode_t **result);
    int (*mkdir)(vnode_t *dir, const char *name, uint32_t mode, vnode_t **result);
    int (*rmdir)(vnode_t *dir, const char *name);
    int (*unlink)(vnode_t *dir, const char *name);
    int (*rename)(vnode_t *old_dir, const char *old_name, vnode_t *new_dir, const char *new_name);
    int (*readdir)(vnode_t *dir, vfs_dirent_t *dirent, uint64_t *offset);
    
    int (*symlink)(vnode_t *dir, const char *name, const char *target);
    int (*readlink)(vnode_t *vnode, char *buf, size_t bufsize);
    int (*link)(vnode_t *dir, const char *name, vnode_t *target);
    
    int (*getattr)(vnode_t *vnode, vfs_stat_t *stat);
    int (*setattr)(vnode_t *vnode, vfs_stat_t *stat);
    
    int (*ioctl)(vnode_t *vnode, unsigned int cmd, unsigned long arg);
    int (*mmap)(vnode_t *vnode, uint64_t addr, size_t len, int prot, int flags);
    
    int (*open)(vnode_t *vnode, uint32_t flags);
    int (*close)(vnode_t *vnode);
    void (*release)(vnode_t *vnode);  /* Called when refcount reaches 0 */
} vnode_ops_t;

typedef struct vfs_filesystem_ops {
    const char *fs_name;            /* Filesystem type name */
    
    int (*mount)(struct blk_device *dev, uint32_t flags, vfs_mount_t **mount);
    int (*unmount)(vfs_mount_t *mount);
    
    int (*alloc_vnode)(vfs_mount_t *mount, vnode_t **vnode);
    void (*free_vnode)(vnode_t *vnode);
    
    int (*sync_fs)(vfs_mount_t *mount);
    int (*statfs)(vfs_mount_t *mount, void *buf);
} vfs_filesystem_ops_t;

void vfs_init(void);

int vfs_register_filesystem(const vfs_filesystem_ops_t *fs_ops);
int vfs_unregister_filesystem(const char *fs_name);

int vfs_mount(const char *device, const char *mountpoint, const char *fstype, uint32_t flags);
int vfs_unmount(const char *mountpoint);
vfs_mount_t *vfs_find_mount(const char *path);

vnode_t *vfs_vnode_alloc(vfs_mount_t *mount);
void vfs_vnode_ref(vnode_t *vnode);
void vfs_vnode_unref(vnode_t *vnode);

int vfs_lookup(const char *path, vnode_t **result);
int vfs_lookup_parent(const char *path, vnode_t **parent, char *name);

int vfs_open(const char *path, uint32_t flags, uint32_t mode, vfs_file_t **file);
int vfs_close(vfs_file_t *file);
int vfs_read(vfs_file_t *file, void *buf, size_t len);
int vfs_write(vfs_file_t *file, const void *buf, size_t len);
int64_t vfs_lseek(vfs_file_t *file, int64_t offset, int whence);
int vfs_stat(const char *path, vfs_stat_t *stat);
int vfs_fstat(vfs_file_t *file, vfs_stat_t *stat);
int vfs_truncate(const char *path, uint64_t size);
int vfs_ftruncate(vfs_file_t *file, uint64_t size);
int vfs_sync(vfs_file_t *file);
int vfs_ioctl(vfs_file_t *file, unsigned int cmd, unsigned long arg);

int vfs_mkdir(const char *path, uint32_t mode);
int vfs_rmdir(const char *path);
int vfs_readdir(vfs_file_t *file, vfs_dirent_t *dirent);
int vfs_create(const char *path, uint32_t mode, vfs_file_t **file);

int vfs_link(const char *oldpath, const char *newpath);
int vfs_unlink(const char *path);
int vfs_symlink(const char *target, const char *linkpath);
int vfs_readlink(const char *path, char *buf, size_t bufsize);
int vfs_rename(const char *oldpath, const char *newpath);

int vfs_chdir(const char *path);
int vfs_getcwd(char *buf, size_t size);

int vfs_access(const char *path, uint32_t mode);
int vfs_chmod(const char *path, uint32_t mode);
int vfs_chown(const char *path, uint32_t uid, uint32_t gid);

int vfs_path_normalize(const char *path, char *normalized, size_t size);
int vfs_path_split(const char *path, char *dir, char *name);
int vfs_path_join(const char *dir, const char *name, char *result, size_t size);
bool vfs_path_is_absolute(const char *path);

void vfs_dump_mounts(void);
void vfs_dump_vnodes(void);
void vfs_print_stats(void);

#endif
