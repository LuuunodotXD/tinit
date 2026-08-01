#ifndef TINIT_H
#define TINIT_H

/*
 * tinit — tiny init system
 * Portable C99, Linux only (proc/sys/mount are Linux-specific).
 * Targets: glibc, musl, uclibc  ×  x86/x86_64/aarch64/armhf/armv7/
 *          loongarch64/ppc64le/riscv64/s390x
 */

#include <sys/types.h>
#include <stddef.h>

/* ------------------------------------------------------------------ */
/* Version & paths                                                     */
/* ------------------------------------------------------------------ */
#define TINIT_VERSION   "0.2.0"
#define INITTAB_PATH    "/etc/inittab"
#define CONSOLE_DEV     "/dev/console"
#define NULL_DEV        "/dev/null"
#define SHELL_PATH      "/bin/sh"
#define INITSCRIPT_PATH "/etc/init.d/rcS"   /* fallback if no inittab */

/* ------------------------------------------------------------------ */
/* Limits                                                               */
/* ------------------------------------------------------------------ */
#define MAX_ARGS        32
#define MAX_LINE        512
#define MAX_ID          16
#define MAX_RUNLEVELS   8
#define MAX_ENTRIES     64

/* ------------------------------------------------------------------ */
/* Reboot commands (mapped to RB_* in sig.c / main.c)                 */
/* ------------------------------------------------------------------ */
#define TINIT_CMD_NONE      0
#define TINIT_CMD_HALT      1
#define TINIT_CMD_REBOOT    2
#define TINIT_CMD_POWEROFF  3
#define TINIT_CMD_RESTART   4   /* re-exec init */

/* ------------------------------------------------------------------ */
/* inittab actions (busybox-compatible naming)                         */
/* ------------------------------------------------------------------ */
typedef enum {
    A_SYSINIT   = 0, /* run once at boot, before everything, wait   */
    A_BOOT,          /* run once at boot, don't wait                 */
    A_BOOTWAIT,      /* run once at boot, wait                       */
    A_WAIT,          /* run and wait                                  */
    A_ONCE,          /* run once, don't wait, no restart             */
    A_RESPAWN,       /* restart when exits                           */
    A_ASKFIRST,      /* like respawn, but prompt "Press Enter..."    */
    A_CTRLALTDEL,    /* on SIGINT to PID 1 (kernel Ctrl+Alt+Del)    */
    A_SHUTDOWN,      /* on orderly shutdown                          */
    A_RESTART,       /* re-exec init                                 */
    A_UNKNOWN
} action_t;

/* ------------------------------------------------------------------ */
/* One parsed inittab line                                             */
/* ------------------------------------------------------------------ */
typedef struct {
    char     id[MAX_ID];
    char     runlevels[MAX_RUNLEVELS];
    action_t action;
    char     process[MAX_LINE];   /* raw command string */
    pid_t    pid;                 /* 0 = not running    */
    int      exited;              /* needs restart?     */
} inittab_entry_t;

/* ------------------------------------------------------------------ */
/* Global init state (single instance)                                 */
/* ------------------------------------------------------------------ */
typedef struct {
    inittab_entry_t entries[MAX_ENTRIES];
    int             num_entries;
    int             running;     /* main loop flag   */
    int             reboot_cmd;  /* TINIT_CMD_*      */
} init_state_t;

extern init_state_t g_state;

/* ------------------------------------------------------------------ */
/* Portability helpers                                                  */
/* ------------------------------------------------------------------ */
#if defined(__GNUC__) || defined(__clang__)
#  define NORETURN          __attribute__((noreturn))
#  define UNUSED            __attribute__((unused))
#  define PRINTF_FMT(f,a)  __attribute__((format(printf, (f), (a))))
#else
#  define NORETURN
#  define UNUSED
#  define PRINTF_FMT(f,a)
#endif

/* Avoid TEMP_FAILURE_RETRY (glibc extension) — use explicit loops   */
#define EINTR_LOOP(ret, expr)  do { (ret) = (expr); } while ((ret) == -1 && errno == EINTR)

#endif /* TINIT_H */
