#ifndef _SYSDIR_H
#define _SYSDIR_H

#include <klibc/types.h>

#include <fs/sysfs.h>

#define SYSDEV_SUBSYS_DEVICES   "/sys/devices"   /* generic devices        */
#define SYSDEV_SUBSYS_BUS       "/sys/bus"        /* bus drivers            */
#define SYSDEV_SUBSYS_CLASS     "/sys/class"      /* device classes         */
#define SYSDEV_SUBSYS_KERNEL    "/sys/kernel"     /* kernel-internal info   */
#define SYSDEV_SUBSYS_FS        "/sys/fs"         /* per-filesystem info    */

#define SYSDEV_SUBSYS_PCI_DEVS  "/sys/bus/pci/devices"
#define SYSDEV_SUBSYS_CLASS_BLOCK  "/sys/class/block"

#define SYSFS_ATTR_SENTINEL  { .name = NULL, .mode = 0, .show = NULL, .store = NULL }

typedef struct sysdev {
    /* [required] short name — becomes the directory basename
     *   e.g. "cpu0"  ->  /sys/devices/cpu0                        */
    const char *name;

    const char *subsys;

    sysfs_attr_t *attrs;

    int (*init)(struct sysdev *dev);

    void (*exit)(struct sysdev *dev);

    void *priv;

    bool registered;
    char path[VFS_PATH_MAX];  /* full sysfs path, e.g. /sys/devices/cpu0 */
} sysdev_t;

int sysdev_register(sysdev_t *dev);
int sysdev_unregister(sysdev_t *dev);

int sysdir_init(void);

int sysdev_register_cpu(void);   /* /sys/devices/cpu0           */
int sysdev_register_pci(void);   /* /sys/bus/pci/devices/<dev>  */
int sysdev_register_fs(void);   /* /sys/fs/tmpfs/ */
int sysdev_register_class(void);   /* /sys/class/block/<dev> */

#endif
