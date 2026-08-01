/*
 * main.c — tinit: tiny, portable Linux init system
 *
 * Boot sequence:
 *   0. Parse /proc/cmdline
 *   1. Remount root RW, mount essential filesystems
 *   2. Load /etc/environment
 *   3. Load /etc/sysctl.conf + /etc/sysctl.d/
 *   4. Open /dev/watchdog + /dev/initctl
 *   5. Parse /etc/inittab (or built-in default)
 *   6. sysinit entries (serial, wait)
 *   7. boot/bootwait entries
 *   8. Main loop: respawn, select(sig_pipe + initctl), watchdog_tick()
 *   9. Shutdown: shutdown entries → SIGTERM → SIGKILL → umount → reboot(2)
 *
 * If called as "switch_root NEWROOT INIT [ARGS...]":
 *   Performs initramfs → real root handoff (see switchroot.c).
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
#include <libgen.h>

#include "tinit.h"
#include "log.h"
#include "mount.h"
#include "sig.h"
#include "process.h"
#include "inittab.h"
#include "cmdline.h"
#include "initctl.h"
#include "switchroot.h"
#include "watchdog.h"
#include "sysctl.h"
#include "env.h"

/* ------------------------------------------------------------------ */
init_state_t g_state;

/* ------------------------------------------------------------------ */
static void silence_kernel_log(void)
{
    int fd = open("/proc/sys/kernel/printk", O_WRONLY);
    if (fd >= 0) {
        (void)write(fd, "1 1 1 1\n", 8);
        close(fd);
    }
}

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
static void run_boot(void)
{
    int i;
    for (i = 0; i < g_state.num_entries; i++) {
        inittab_entry_t *e = &g_state.entries[i];
        if (e->process[0] == '\0') continue;
        switch (e->action) {
        case A_BOOT:                          spawn_entry(e);            break;
        case A_BOOTWAIT: case A_WAIT:
            log_info("bootwait: %s", e->process);
            run_and_wait(e->process);
            break;
        case A_ONCE:                          spawn_entry(e);            break;
        default: break;
        }
    }
}

/* ------------------------------------------------------------------ */
static void start_respawn(void)
{
    int i;
    for (i = 0; i < g_state.num_entries; i++) {
        inittab_entry_t *e = &g_state.entries[i];
        if ((e->action == A_RESPAWN || e->action == A_ASKFIRST) &&
            e->pid == 0 && e->process[0] != '\0')
            spawn_entry(e);
    }
}

/* ------------------------------------------------------------------ */
static void handle_signal_byte(unsigned char signo)
{
    int i;
    switch ((int)signo) {
    case SIGCHLD:
        reap_children();
        break;
    case SIGINT:
        for (i = 0; i < g_state.num_entries; i++) {
            inittab_entry_t *e = &g_state.entries[i];
            if (e->action == A_CTRLALTDEL && e->process[0] != '\0')
                run_and_wait(e->process);
        }
        log_info("SIGINT (Ctrl+Alt+Del) — rebooting");
        g_state.reboot_cmd = TINIT_CMD_REBOOT;
        g_state.running    = 0;
        break;
    case SIGTERM:
    case SIGUSR1:
        log_info("SIGTERM/SIGUSR1 — halting");
        g_state.reboot_cmd = TINIT_CMD_HALT;
        g_state.running    = 0;
        break;
    case SIGUSR2:
        log_info("SIGUSR2 — powering off");
        g_state.reboot_cmd = TINIT_CMD_POWEROFF;
        g_state.running    = 0;
        break;
    case SIGHUP:
        log_info("SIGHUP — reloading inittab");
        {
            int n = inittab_parse(INITTAB_PATH, g_state.entries, MAX_ENTRIES);
            if (n > 0) g_state.num_entries = n;
        }
        break;
    default:
        break;
    }
}

/* ------------------------------------------------------------------ */
static void do_shutdown(void)
{
    int i;
    log_info("shutdown sequence starting");

    for (i = 0; i < g_state.num_entries; i++) {
        inittab_entry_t *e = &g_state.entries[i];
        if (e->action == A_SHUTDOWN && e->process[0] != '\0')
            run_and_wait(e->process);
    }

    initctl_close();
    watchdog_close();   /* disable watchdog BEFORE killing children */

    log_info("sending SIGTERM to all processes");
    kill(-1, SIGTERM);
    sleep(1);
    log_info("sending SIGKILL to all processes");
    kill(-1, SIGKILL);
    sleep(1);

    reap_children();
    umount_all();
}

/* ------------------------------------------------------------------ */
static NORETURN void do_reboot(int cmd)
{
    switch (cmd) {
    case TINIT_CMD_REBOOT:   log_info("rebooting...");    reboot(RB_AUTOBOOT);    break;
    case TINIT_CMD_POWEROFF: log_info("powering off..."); reboot(RB_POWER_OFF);   break;
    default:                 log_info("halting...");      reboot(RB_HALT_SYSTEM); break;
    }
    for (;;) pause();
}

/* ------------------------------------------------------------------ */
static void main_loop(void)
{
    g_state.running = 1;
    log_info("entering main loop");

    while (g_state.running) {
        fd_set         rfds;
        struct timeval tv;
        int            nfds, ret;

        start_respawn();

        FD_ZERO(&rfds);
        FD_SET(sig_pipe_r, &rfds);
        nfds = sig_pipe_r + 1;

        if (initctl_fd >= 0) {
            FD_SET(initctl_fd, &rfds);
            if (initctl_fd >= nfds) nfds = initctl_fd + 1;
        }

        /* 1-second timeout for watchdog_tick() */
        tv.tv_sec  = 1;
        tv.tv_usec = 0;

        ret = select(nfds, &rfds, NULL, NULL, &tv);
        if (ret < 0) {
            if (errno == EINTR) continue;
            log_error("select: %s", strerror(errno));
            continue;
        }

        /* Kick watchdog every second regardless of what woke us */
        watchdog_tick();

        if (ret == 0) continue;  /* timeout — nothing more to do */

        if (initctl_fd >= 0 && FD_ISSET(initctl_fd, &rfds))
            initctl_handle();

        if (FD_ISSET(sig_pipe_r, &rfds)) {
            unsigned char signo;
            ssize_t n;
            for (;;) {
                n = read(sig_pipe_r, &signo, 1);
                if (n == 1)       handle_signal_byte(signo);
                else if (n < 0 && errno == EINTR) continue;
                else              break;
            }
        }
    }
}

/* ------------------------------------------------------------------ */
int main(int argc, char *argv[])
{
    int is_pid1 = (getpid() == 1);
    int n;

    /* ── switch_root applet? ── */
    {
        const char *name = basename(argv[0]);
        if (name && strcmp(name, "switch_root") == 0)
            return switch_root_main(argc, argv);
    }

    /* ── Phase 0: console + cmdline ── */
    log_init();
    log_info("tinit %s starting (pid=%d)", TINIT_VERSION, (int)getpid());

    if (is_pid1) {
        cmdline_parse();
        if (g_cmdline.quiet)   silence_kernel_log();
        if (g_cmdline.debug)   cmdline_dump();
    }

    /* ── Phase 1: signals ── */
    sig_init();

    /* ── Phase 2: filesystems ── */
    if (is_pid1) {
        mount_rootfs_rw();
        mount_early();
        log_close();
        log_init();
    }

    /* ── Phase 3: environment ── */
    env_load_all();
    if (g_cmdline.debug) env_dump();

    /* ── Phase 4: sysctl ── */
    if (is_pid1) sysctl_load_all();

    /* ── Phase 5: watchdog + initctl ── */
    if (is_pid1) {
        watchdog_open();
        initctl_open();
    }

    /* ── Phase 6: inittab ── */
    memset(&g_state, 0, sizeof(g_state));
    n = inittab_parse(INITTAB_PATH, g_state.entries, MAX_ENTRIES);
    if (n <= 0) {
        log_warn("no valid inittab — using built-in default");
        n = inittab_default(g_state.entries, MAX_ENTRIES);
    }
    g_state.num_entries = n;
    if (g_cmdline.debug) inittab_dump(g_state.entries, n);

    /* ── Phase 7: sysinit ── */
    run_sysinit();

    /* ── Phase 8: boot ── */
    run_boot();

    /* ── Phase 9: main loop ── */
    main_loop();

    /* ── Phase 10: shutdown ── */
    do_shutdown();

    if (is_pid1)
        do_reboot(g_state.reboot_cmd);

    return 0;
}
