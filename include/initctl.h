#ifndef INITCTL_H
#define INITCTL_H

/*
 * initctl.h — /dev/initctl FIFO compatibility
 *
 * shutdown(8), halt(8), reboot(8), and poweroff(8) from sysvinit/
 * util-linux communicate with PID 1 by writing a fixed-size struct
 * to /dev/initctl.  We implement the receiving end.
 *
 * Protocol:
 *   - /dev/initctl is a named pipe (FIFO), mode 0600, owned by root
 *   - Tools write exactly sizeof(struct init_request) bytes at once
 *   - PID 1 reads and acts on the command field
 *
 * Reference: sysvinit src/initreq.h (public domain)
 */

#include <sys/types.h>

/* Magic number that must appear in every request */
#define INIT_MAGIC       0x03091969

/* init_request.cmd values */
#define INIT_CMD_START        0   /* unused */
#define INIT_CMD_RUNLVL       1   /* change runlevel (halt=0, reboot=6, ...) */
#define INIT_CMD_POWERFAIL    2   /* UPS: power failing, prepare to go down  */
#define INIT_CMD_POWERFAILNOW 3   /* UPS: power failing NOW                  */
#define INIT_CMD_POWEROK      4   /* UPS: power restored, cancel shutdown    */
#define INIT_CMD_SETENV       6   /* set environment variable (ignored)      */
#define INIT_CMD_SETCONS      7   /* set console device (ignored)            */

/*
 * init_request.runlevel values (used with INIT_CMD_RUNLVL):
 *   '0' → halt
 *   '6' → reboot
 *   's' or 'S' or '1' → single user (spawn rescue shell)
 *   'a'-'c' → on-demand runlevels (ignored)
 *   'q' or 'Q' → reload inittab
 */

/* Wire format — must match sysvinit exactly */
struct init_request {
    int  magic;          /* INIT_MAGIC                                */
    int  cmd;            /* INIT_CMD_*                                */
    int  runlevel;       /* new runlevel / special char (see above)   */
    int  sleeptime;      /* seconds between SIGTERM and SIGKILL       */
    union {
        char  b[368];
        struct {
            char var[16];    /* INIT_CMD_SETENV: "NAME=value\0"       */
            char value[348];
        } e;
    } data;
};

#define INIT_REQUEST_SIZE  ((int)sizeof(struct init_request))

/* fd for the open FIFO (-1 if not open) */
extern int initctl_fd;

/*
 * Create /dev/initctl if needed, open it, return fd.
 * Returns -1 on error (non-fatal: init still works via signals).
 */
int  initctl_open(void);

/*
 * Read and handle one (or more) pending requests from initctl_fd.
 * Translates requests into g_state.reboot_cmd + g_state.running = 0.
 * Call this when select() reports initctl_fd readable.
 */
void initctl_handle(void);

void initctl_close(void);

#endif /* INITCTL_H */
