#ifndef SWITCHROOT_H
#define SWITCHROOT_H

/*
 * switchroot.h — initramfs → real rootfs handoff
 *
 * Used when tinit starts as PID 1 inside an initramfs and needs to
 * transition to the real root filesystem before running the final init.
 *
 * Two strategies are tried in order:
 *
 *   1. pivot_root(2)  [preferred]
 *      Atomically swaps the root mount.  Requires newroot to already
 *      be a distinct mountpoint (i.e., something was mounted there).
 *      Old root is lazily unmounted after exec.
 *
 *   2. MS_MOVE + chroot  [fallback]
 *      Moves the newroot mount to "/" and chroots into it.
 *      Used when pivot_root fails (e.g., newroot == current root).
 *
 * After the root switch, we exec the new init without returning.
 * If exec fails, we return a non-zero error code so the caller can
 * drop into a rescue shell.
 *
 * Usage as a standalone binary (busybox-style applet):
 *   switch_root NEWROOT INIT [ARGS...]
 *
 * Usage as a library call from inside tinit:
 *   switch_root("/newroot", "/sbin/init", argv);
 */

/*
 * Perform the root switch and exec new init.
 *
 *   newroot  — path to the new root (must already be a mountpoint)
 *   init     — path of the init to exec INSIDE newroot
 *   argv     — argv to pass to init (argv[0] = init, NULL-terminated)
 *
 * Returns -1 on error (caller should drop to rescue shell).
 * On success this function never returns (exec replaces the process).
 */
int do_switch_root(const char *newroot, const char *init, char *const argv[]);

/*
 * Entry point when tinit is invoked as "switch_root":
 *   argv[0] = "switch_root"
 *   argv[1] = newroot
 *   argv[2] = init
 *   argv[3..] = init args (optional)
 *
 * Returns exit code (0 = success, but we normally never return).
 */
int switch_root_main(int argc, char *argv[]);

#endif /* SWITCHROOT_H */
