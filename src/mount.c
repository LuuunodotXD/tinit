/*
 * mount.c — early-boot filesystem mounting for tinit
 *
 * We use the raw mount(2) syscall.  No libmount, no /etc/fstab parsing.
 * Errors from already-mounted filesystems are silently ignored.
 */


#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/sysmacros.h>  /* makedev() — Linux extension */
#include <errno.h>
#include <string.h>
#include <unistd.h>         /* sync(), umount2() */
#include <fcntl.h>

#include "tinit.h"
#include "log.h"
#include "mount.h"

/* ------------------------------------------------------------------ */
/* Helper: mount with error reporting                                   */
/* ------------------------------------------------------------------ */
static void do_mount(const char *src,
                     const char *tgt,
                     const char *type,
                     unsigned long flags,
                     const void  *data)
{
    /* Create mount point if it does not exist */
    if (mkdir(tgt, 0755) < 0 && errno != EEXIST)
        log_warn("mkdir %s: %s", tgt, strerror(errno));

    if (mount(src, tgt, type, flags, data) < 0) {
        if (errno == EBUSY)
            return;   /* already mounted — fine */
        log_warn("mount %s -> %s (%s): %s", src, tgt, type, strerror(errno));
    } else {
        log_info("mounted %s on %s", type, tgt);
    }
}

/* ------------------------------------------------------------------ */

void mount_early(void)
{
    /* proc — process/kernel info */
    do_mount("proc",    "/proc",    "proc",     MS_NOSUID|MS_NOEXEC|MS_NODEV, NULL);

    /* sysfs — device/driver info */
    do_mount("sysfs",   "/sys",     "sysfs",    MS_NOSUID|MS_NOEXEC|MS_NODEV, NULL);

    /* devtmpfs — device nodes (kernel auto-populates) */
    /* Falls back to tmpfs + manual mknod if not available */
    if (mount("devtmpfs", "/dev", "devtmpfs",
              MS_NOSUID | MS_STRICTATIME,
              "mode=0755,size=10M") < 0) {
        if (errno != EBUSY) {
            log_warn("devtmpfs not available, mounting tmpfs on /dev");
            do_mount("tmpfs", "/dev", "tmpfs",
                     MS_NOSUID | MS_STRICTATIME, "mode=0755,size=10M");
            /* Create minimum device nodes */
            mknod("/dev/null",    S_IFCHR|0666, makedev(1,3));
            mknod("/dev/zero",    S_IFCHR|0666, makedev(1,5));
            mknod("/dev/full",    S_IFCHR|0666, makedev(1,7));
            mknod("/dev/random",  S_IFCHR|0666, makedev(1,8));
            mknod("/dev/urandom", S_IFCHR|0666, makedev(1,9));
            mknod("/dev/tty",     S_IFCHR|0666, makedev(5,0));
            mknod("/dev/console", S_IFCHR|0600, makedev(5,1));
            mknod("/dev/ptmx",    S_IFCHR|0666, makedev(5,2));
        }
    } else {
        log_info("mounted devtmpfs on /dev");
    }

    /* devpts — pseudo-terminal master/slave pairs */
    do_mount("pts",     "/dev/pts", "devpts",
             MS_NOSUID | MS_NOEXEC,
             "mode=0620,ptmxmode=0666,gid=5");

    /* tmpfs on /dev/shm — POSIX shared memory */
    do_mount("shm",     "/dev/shm", "tmpfs",
             MS_NOSUID | MS_NODEV, "mode=1777");

    /* tmpfs on /run — runtime data (PID files, sockets, ...) */
    do_mount("tmpfs",   "/run",     "tmpfs",
             MS_NOSUID | MS_NODEV | MS_STRICTATIME,
             "mode=0755,size=10%");

    /* tmpfs on /tmp */
    do_mount("tmpfs",   "/tmp",     "tmpfs",
             MS_NOSUID | MS_NODEV | MS_STRICTATIME,
             "mode=1777,size=20%");
}

/* ------------------------------------------------------------------ */

void mount_rootfs_rw(void)
{
    if (mount(NULL, "/", NULL, MS_REMOUNT | MS_RELATIME, NULL) < 0)
        log_warn("remount / rw: %s", strerror(errno));
    else
        log_info("root filesystem remounted read-write");
}

/* ------------------------------------------------------------------ */

void umount_all(void)
{
    /*
     * Unmount in reverse order.  Errors are non-fatal — the kernel
     * will handle it on hard reset / halt.
     */
    static const char * const mpoints[] = {
        "/run", "/tmp", "/dev/shm", "/dev/pts", "/dev", "/sys", "/proc",
        NULL
    };
    int i;

    log_info("unmounting filesystems...");

    /* Sync first */
    sync();

    for (i = 0; mpoints[i] != NULL; i++) {
        if (umount2(mpoints[i], MNT_DETACH) < 0)
            log_warn("umount %s: %s", mpoints[i], strerror(errno));
    }

    /* Remount root read-only */
    if (mount(NULL, "/", NULL, MS_REMOUNT | MS_RDONLY, NULL) < 0)
        log_warn("remount / ro: %s", strerror(errno));
    else
        log_info("root filesystem remounted read-only");

    sync();
}
