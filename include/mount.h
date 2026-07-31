#ifndef MOUNT_H
#define MOUNT_H

/*
 * Early-boot filesystem mounting.
 * All calls use the raw mount(2) syscall — no libmount dependency.
 */

/* Mount proc, sysfs, devtmpfs, devpts, /run (tmpfs) */
void mount_early(void);

/* Remount root filesystem read-write */
void mount_rootfs_rw(void);

/* Unmount everything cleanly before reboot/halt */
void umount_all(void);

#endif /* MOUNT_H */
