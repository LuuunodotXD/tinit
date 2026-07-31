/*
 * main.c — tinit: tiny, portable Linux init system
 *
 * Design goals:
 *   - Busybox-compatible /etc/inittab
 *   - C99, no compiler/libc extensions beyond POSIX.1-2008
 *   - Works as PID 1 and as a regular process (for testing)
 *   - All libc targets: glibc, musl, uclibc
 *
 * Boot sequence:
 *   1. Remount root RW, mount essential filesystems
 *   2. Parse /etc/inittab (or use built-in default)
 *   3. Run all sysinit entries (serial, wait for each)
 *   4. Run all boot/bootwait entries
 *   5. Main loop: respawn dead processes, handle signals
 *   6. On shutdown signal: run shutdown entries, umount, halt/reboot
 */


#include <sys/types.h>
#include <sys/reboot.h>
#include <sys/select.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <stdio.h>

#include "tinit.h"
#include "log.h"
#include "mount.h"
#include "sig.h"
#include "process.h"
#include "inittab.h"

/* ------------------------------------------------------------------ */
/* Global state (single init instance)                                 */
/* ------------------------------------------------------------------ */
init_state_t g_state;

/* ------------------------------------------------------------------ */
/* Suppress kernel console messages during boot by raising printk level*/
/* ------------------------------------------------------------------ */
static void silence_kernel_log(void)
{
    int fd = open("/proc/sys/kernel/printk", O_WRONLY);
    if (fd >= 0) {
        /* 1 1 1 1 — only panic-level messages */
        (void)write(fd, "1 1 1 1\n", 8);
        close(fd);
    }
}

/* ------------------------------------------------------------------ */
/* Phase 1: sysinit entries — run serially and wait                    */
/* ------------------------------------------------------------------ */
static void run_sysinit(void)
{
    int i;
    for (i = 0; i < g_state.num_entries; i++) {
        inittab_entry_t *e = &g_state.entries[i];
        if (e->action != A_SYSINIT || e->process[0] == '\0')
            continue;
        log_info("sysinit: %s", e->process);
        run_and_wait(e->process);
    }
}

/* ------------------------------------------------------------------ */
/* Phase 2: boot entries (boot = no wait, bootwait = wait)             */
/* ------------------------------------------------------------------ */
static void run_boot(void)
{
    int i;
    for (i = 0; i < g_state.num_entries; i++) {
        inittab_entry_t *e = &g_state.entries[i];
        if (e->process[0] == '\0') continue;

        if (e->action == A_BOOT) {
            spawn_entry(e);
        } else if (e->action == A_BOOTWAIT || e->action == A_WAIT) {
            log_info("bootwait: %s", e->process);
            run_and_wait(e->process);
        } else if (e->action == A_ONCE) {
            spawn_entry(e);
        }
    }
}

/* ------------------------------------------------------------------ */
/* Phase 3: main loop — start respawn entries, handle signals          */
/* ------------------------------------------------------------------ */
static void start_respawn(void)
{
    int i;
    for (i = 0; i < g_state.num_entries; i++) {
        inittab_entry_t *e = &g_state.entries[i];
        if ((e->action == A_RESPAWN || e->action == A_ASKFIRST) &&
            e->pid == 0 && e->process[0] != '\0') {
            spawn_entry(e);
        }
    }
}

/* ------------------------------------------------------------------ */
/* Handle one byte from the signal pipe                                */
/* ------------------------------------------------------------------ */
static void handle_signal_byte(unsigned char signo)
{
    int i;

    switch (signo) {

    case SIGCHLD:
        reap_children();
        break;

    case SIGINT:
        /* Ctrl+Alt+Del → reboot */
        log_info("SIGINT: Ctrl+Alt+Del received — rebooting");
        g_state.reboot_cmd = TINIT_CMD_REBOOT;
        g_state.running    = 0;
        break;

    case SIGTERM:
    case SIGUSR1:
        log_info("SIGTERM/SIGUSR1: halting");
        g_state.reboot_cmd = TINIT_CMD_HALT;
        g_state.running    = 0;
        break;

    case SIGUSR2:
        log_info("SIGUSR2: powering off");
        g_state.reboot_cmd = TINIT_CMD_POWEROFF;
        g_state.running    = 0;
        break;

    case SIGHUP:
        /* Reload inittab */
        log_info("SIGHUP: reloading inittab");
        {
            int n = inittab_parse(INITTAB_PATH,
                                  g_state.entries,
                                  MAX_ENTRIES);
            if (n > 0) g_state.num_entries = n;
        }
        break;

    default:
        /* ctrlaltdel entry might also register SIGINT separately */
        for (i = 0; i < g_state.num_entries; i++) {
            inittab_entry_t *e = &g_state.entries[i];
            if (e->action == A_CTRLALTDEL && signo == SIGINT)
                run_and_wait(e->process);
        }
        break;
    }
}

/* ------------------------------------------------------------------ */
/* Shutdown: run shutdown entries, kill all processes, umount          */
/* ------------------------------------------------------------------ */
static void do_shutdown(void)
{
    int i;

    log_info("starting shutdown sequence");

    /* Run shutdown entries */
    for (i = 0; i < g_state.num_entries; i++) {
        inittab_entry_t *e = &g_state.entries[i];
        if (e->action == A_SHUTDOWN && e->process[0] != '\0')
            run_and_wait(e->process);
    }

    /* Ask all processes to terminate */
    log_info("sending SIGTERM to all processes");
    kill(-1, SIGTERM);
    sleep(1);

    /* Force-kill survivors */
    log_info("sending SIGKILL to all processes");
    kill(-1, SIGKILL);
    sleep(1);

    /* Reap remaining children */
    reap_children();

    /* Unmount filesystems and sync */
    umount_all();
}

/* ------------------------------------------------------------------ */
/* The actual reboot/halt/poweroff syscall                             */
/* ------------------------------------------------------------------ */
static NORETURN void do_reboot(int cmd)
{
    switch (cmd) {
    case TINIT_CMD_HALT:
        log_info("halting system");
        reboot(RB_HALT_SYSTEM);
        break;
    case TINIT_CMD_REBOOT:
        log_info("rebooting system");
        reboot(RB_AUTOBOOT);
        break;
    case TINIT_CMD_POWEROFF:
        log_info("powering off");
        reboot(RB_POWER_OFF);
        break;
    default:
        log_info("halting (unknown cmd)");
        reboot(RB_HALT_SYSTEM);
        break;
    }
    /* Should never reach here */
    for (;;) pause();
}

/* ------------------------------------------------------------------ */
/* Main loop                                                           */
/* ------------------------------------------------------------------ */
static void main_loop(void)
{
    g_state.running = 1;

    log_info("entering main loop");

    while (g_state.running) {
        fd_set rfds;
        struct timeval tv;
        int ret;

        /* (Re)start any respawn entries that have died */
        start_respawn();

        /* Wait for signal pipe or timeout (1s for poll-based fallback) */
        FD_ZERO(&rfds);
        FD_SET(sig_pipe_r, &rfds);
        tv.tv_sec  = 1;
        tv.tv_usec = 0;

        ret = select(sig_pipe_r + 1, &rfds, NULL, NULL, &tv);
        if (ret < 0) {
            if (errno == EINTR) continue;
            log_error("select: %s", strerror(errno));
            continue;
        }

        if (ret > 0 && FD_ISSET(sig_pipe_r, &rfds)) {
            unsigned char signo;
            ssize_t n;

            /* Drain all pending signal bytes */
            for (;;) {
                n = read(sig_pipe_r, &signo, 1);
                if (n == 1) {
                    handle_signal_byte(signo);
                } else if (n < 0 && errno == EINTR) {
                    continue;
                } else {
                    break;
                }
                /* After SIGCHLD, re-check immediately */
                if (signo == SIGCHLD) break;
            }
        }
    }
}

/* ------------------------------------------------------------------ */
/* Entry point                                                         */
/* ------------------------------------------------------------------ */
int main(int argc, char *argv[])
{
    int is_pid1 = (getpid() == 1);
    int n;

    (void)argc;
    (void)argv;

    /* ── Phase 0: console ── */
    log_init();
    log_info("tinit %s starting (pid=%d, is_pid1=%d)",
             TINIT_VERSION, (int)getpid(), is_pid1);

    /* ── Phase 1: signals ── */
    sig_init();

    /* ── Phase 2: filesystems ── */
    if (is_pid1) {
        mount_rootfs_rw();
        mount_early();
        silence_kernel_log();
        /* Re-open console after /dev is mounted */
        log_close();
        log_init();
    }

    /* ── Phase 3: parse inittab ── */
    memset(&g_state, 0, sizeof(g_state));

    n = inittab_parse(INITTAB_PATH, g_state.entries, MAX_ENTRIES);
    if (n <= 0) {
        log_warn("no valid inittab, using built-in default");
        n = inittab_default(g_state.entries, MAX_ENTRIES);
    }
    g_state.num_entries = n;
    inittab_dump(g_state.entries, n);

    /* ── Phase 4: sysinit ── */
    run_sysinit();

    /* ── Phase 5: boot ── */
    run_boot();

    /* ── Phase 6: main loop ── */
    main_loop();

    /* ── Phase 7: shutdown ── */
    do_shutdown();

    if (is_pid1)
        do_reboot(g_state.reboot_cmd);
    else
        return 0;

    /* unreachable */
    return 0;
}
