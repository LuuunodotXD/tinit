/*
 * switchroot.c — initramfs → real rootfs handoff for tinit
 *
 * Algorithm (pivot_root path):
 *   1. chdir(newroot)
 *   2. Create newroot/.put_old as the pivot point
 *   3. pivot_root(".", ".put_old")
 *   4. chdir("/")
 *   5. Lazily unmount /.put_old (the old initramfs)
 *   6. rmdir("/.put_old")
 *   7. exec new init
 *
 * Algorithm (MS_MOVE fallback):
 *   1. chdir(newroot)
 *   2. mount(".", "/", NULL, MS_MOVE, NULL)
 *   3. chroot(".")
 *   4. chdir("/")
 *   5. exec new init
 *
 * In both cases the initramfs contents stay in RAM until the old root
 * reference count drops to zero, which happens automatically.
 *
 * NOTE: pivot_root(2) requires newroot to be a real mountpoint
 *       (different device or mount namespace entry than current root).
 *       If the initramfs IS the root (no new mount over /), only the
 *       MS_MOVE path works.
 */


#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mount.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "tinit.h"
#include "log.h"
#include "mount.h"
#include "sig.h"
#include "switchroot.h"

/* ------------------------------------------------------------------ */
/* pivot_root(2) — not in older glibc headers, call via syscall(2)    */
/* ------------------------------------------------------------------ */
static int pivot_root(const char *new_root, const char *put_old)
{
    return (int)syscall(SYS_pivot_root, new_root, put_old);
}

/* ------------------------------------------------------------------ */
/* Move proc/sys/dev mounts from old root into newroot                 */
/* (so the new init finds them)                                        */
/* ------------------------------------------------------------------ */
static void move_mounts(const char *newroot)
{
    static const char * const mnts[] = {
        "/proc", "/sys", "/dev", "/run", "/tmp", NULL
    };
    char dst[512];
    int  i;

    for (i = 0; mnts[i]; i++) {
        snprintf(dst, sizeof(dst), "%s%s", newroot, mnts[i]);

        /* Create target dir if missing */
        if (mkdir(dst, 0755) < 0 && errno != EEXIST)
            log_warn("switchroot: mkdir %s: %s", dst, strerror(errno));

        if (mount(mnts[i], dst, NULL, MS_MOVE, NULL) < 0) {
            /*
             * MS_MOVE can fail if the fs isn't mounted.
             * That's fine — just skip it.
             */
            if (errno != EINVAL && errno != ENOENT)
                log_warn("switchroot: move %s → %s: %s",
                         mnts[i], dst, strerror(errno));
        } else {
            log_info("switchroot: moved %s → %s", mnts[i], dst);
        }
    }
}

/* ------------------------------------------------------------------ */
/* Strategy 1: pivot_root                                              */
/* ------------------------------------------------------------------ */
static int try_pivot_root(const char *newroot)
{
    char put_old[512];

    snprintf(put_old, sizeof(put_old), "%s/.put_old", newroot);

    if (mkdir(put_old, 0700) < 0 && errno != EEXIST) {
        log_warn("switchroot: mkdir %s: %s", put_old, strerror(errno));
        return -1;
    }

    if (pivot_root(newroot, put_old) < 0) {
        /* Common reason: newroot not a distinct mountpoint */
        rmdir(put_old);
        log_info("switchroot: pivot_root failed (%s), trying MS_MOVE",
                 strerror(errno));
        return -1;
    }

    /* Now we're in the new root.  Old root is at /.put_old */
    if (chdir("/") < 0) {
        log_error("switchroot: chdir / after pivot: %s", strerror(errno));
        return -1;
    }

    /* Lazily detach old initramfs */
    if (umount2("/.put_old", MNT_DETACH) < 0)
        log_warn("switchroot: umount /.put_old: %s", strerror(errno));

    rmdir("/.put_old");

    log_info("switchroot: pivot_root succeeded");
    return 0;
}

/* ------------------------------------------------------------------ */
/* Strategy 2: MS_MOVE + chroot                                        */
/* ------------------------------------------------------------------ */
static int try_move_root(const char *newroot)
{
    if (mount(newroot, "/", NULL, MS_MOVE, NULL) < 0) {
        log_error("switchroot: MS_MOVE %s → /: %s",
                  newroot, strerror(errno));
        return -1;
    }

    if (chroot(".") < 0) {
        log_error("switchroot: chroot: %s", strerror(errno));
        return -1;
    }

    if (chdir("/") < 0) {
        log_error("switchroot: chdir /: %s", strerror(errno));
        return -1;
    }

    log_info("switchroot: MS_MOVE + chroot succeeded");
    return 0;
}

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */
int do_switch_root(const char *newroot, const char *init,
                   char *const argv[])
{
    struct stat st_old, st_new;

    /* Sanity: newroot must exist and be a directory */
    if (stat(newroot, &st_new) < 0) {
        log_error("switchroot: stat %s: %s", newroot, strerror(errno));
        return -1;
    }
    if (!S_ISDIR(st_new.st_mode)) {
        log_error("switchroot: %s is not a directory", newroot);
        return -1;
    }

    /* Sanity: init must exist inside newroot */
    {
        char init_path[512];
        snprintf(init_path, sizeof(init_path), "%s%s", newroot, init);
        if (stat(init_path, &st_old) < 0) {
            log_error("switchroot: init '%s' not found in newroot: %s",
                      init, strerror(errno));
            return -1;
        }
    }

    log_info("switchroot: newroot=%s init=%s", newroot, init);

    /* Move /proc /sys /dev /run /tmp into newroot so new init finds them */
    move_mounts(newroot);

    /* chdir into newroot before switching */
    if (chdir(newroot) < 0) {
        log_error("switchroot: chdir %s: %s", newroot, strerror(errno));
        return -1;
    }

    /* Try pivot_root first, fall back to MS_MOVE */
    if (try_pivot_root(newroot) < 0) {
        if (try_move_root(newroot) < 0)
            return -1;
    }

    /* Exec new init — never returns on success */
    log_info("switchroot: exec %s", init);
    log_close();   /* close /dev/console before exec */

    sig_unblock_all();

    execv(init, argv);

    /* exec failed — try sh as last resort */
    {
        const char *sh_argv[] = { SHELL_PATH, NULL };
        log_init();
        log_error("switchroot: exec %s: %s — trying %s",
                  init, strerror(errno), SHELL_PATH);
        log_close();
        execv(SHELL_PATH, (char *const *)sh_argv);
    }

    log_init();
    log_error("switchroot: all exec attempts failed");
    return -1;
}

/* ------------------------------------------------------------------ */
/* Standalone entry point (called when argv[0] == "switch_root")      */
/* ------------------------------------------------------------------ */
int switch_root_main(int argc, char *argv[])
{
    if (argc < 3) {
        log_error("Usage: switch_root NEWROOT INIT [ARGS...]");
        return 1;
    }

    /*
     * argv layout for the new init:
     *   argv[0] = INIT (the init binary)
     *   argv[1..] = rest of our argv (if any)
     *   argv[argc-2] = NULL terminator
     *
     * We pass argv+2 so the new init gets:
     *   argv[0] = INIT path
     *   argv[1..] = any extra args the caller provided
     */
    if (do_switch_root(argv[1], argv[2], argv + 2) < 0) {
        log_error("switch_root failed — dropping to rescue shell");
        {
            char *sh_argv[] = { SHELL_PATH, NULL };
            execv(SHELL_PATH, sh_argv);
        }
        return 1;
    }

    /* Never reached on success */
    return 0;
}
