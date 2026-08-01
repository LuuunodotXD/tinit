/*
 * initctl.c — /dev/initctl FIFO for tinit
 *
 * Allows standard userspace tools (shutdown(8), halt(8), reboot(8))
 * to communicate with us via the sysvinit wire protocol.
 *
 * Opening strategy:
 *   We open the FIFO with O_RDWR so we hold both a reader and a writer
 *   end ourselves.  This prevents select() from returning EOF when the
 *   last userspace writer closes its end — without this trick, the FIFO
 *   would appear "hung up" and select() would busy-loop.
 */


#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <signal.h>

#include "tinit.h"
#include "log.h"
#include "initctl.h"

int initctl_fd = -1;

#define INITCTL_PATH  "/dev/initctl"

/* ------------------------------------------------------------------ */

int initctl_open(void)
{
    struct stat st;

    /* Create the FIFO if it doesn't exist */
    if (stat(INITCTL_PATH, &st) < 0) {
        if (mkfifo(INITCTL_PATH, 0600) < 0 && errno != EEXIST) {
            log_warn("initctl: mkfifo %s: %s", INITCTL_PATH, strerror(errno));
            return -1;
        }
    } else if (!S_ISFIFO(st.st_mode)) {
        /* Something else is there (regular file, symlink...) — replace it */
        unlink(INITCTL_PATH);
        if (mkfifo(INITCTL_PATH, 0600) < 0) {
            log_warn("initctl: mkfifo (replace) %s: %s",
                     INITCTL_PATH, strerror(errno));
            return -1;
        }
    }

    /* Ensure correct permissions regardless of umask */
    chmod(INITCTL_PATH, 0600);

    /*
     * O_RDWR: We hold both ends so the FIFO never reaches EOF from
     *         our perspective when userspace tools close their write end.
     * O_NONBLOCK: Don't block if nobody is writing yet.
     * O_CLOEXEC: Don't leak to children.
     */
    initctl_fd = open(INITCTL_PATH, O_RDWR | O_NONBLOCK | O_CLOEXEC);
    if (initctl_fd < 0) {
        log_warn("initctl: open %s: %s", INITCTL_PATH, strerror(errno));
        return -1;
    }

    log_info("initctl: listening on %s (fd=%d)", INITCTL_PATH, initctl_fd);
    return initctl_fd;
}

/* ------------------------------------------------------------------ */

void initctl_handle(void)
{
    struct init_request req;
    ssize_t n;

    for (;;) {
        /* Non-blocking read of exactly one request struct */
        do {
            n = read(initctl_fd, &req, sizeof(req));
        } while (n < 0 && errno == EINTR);

        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                return;   /* no more data right now */
            log_warn("initctl: read: %s", strerror(errno));
            return;
        }
        if (n == 0)
            return;  /* EOF — shouldn't happen with O_RDWR */

        if (n != sizeof(req)) {
            log_warn("initctl: short read (%zd/%d bytes), discarding",
                     n, INIT_REQUEST_SIZE);
            return;
        }

        /* Validate magic */
        if (req.magic != INIT_MAGIC) {
            log_warn("initctl: bad magic 0x%08x (expected 0x%08x)",
                     req.magic, INIT_MAGIC);
            continue;
        }

        log_info("initctl: cmd=%d runlevel=%c(%d)",
                 req.cmd, (req.runlevel >= 32 ? req.runlevel : '?'),
                 req.runlevel);

        switch (req.cmd) {

        case INIT_CMD_RUNLVL:
            switch (req.runlevel) {

            case '0':
                log_info("initctl: halt requested");
                g_state.reboot_cmd = TINIT_CMD_HALT;
                g_state.running    = 0;
                break;

            case '6':
                log_info("initctl: reboot requested");
                g_state.reboot_cmd = TINIT_CMD_REBOOT;
                g_state.running    = 0;
                break;

            case 's': case 'S': case '1':
                /* Single-user: kill all non-essential processes and
                 * spawn a rescue shell.  For now, treat as halt
                 * if no shell to rescue to. */
                log_info("initctl: single-user mode requested");
                kill(-1, SIGTERM);
                /* TODO: spawn rescue shell, re-enter main loop */
                break;

            case 'q': case 'Q':
                /* Re-read inittab */
                log_info("initctl: reloading inittab");
                kill(1, SIGHUP);
                break;

            default:
                log_warn("initctl: unknown runlevel '%c' (%d)",
                         req.runlevel >= 32 ? req.runlevel : '?',
                         req.runlevel);
                break;
            }
            break;

        case INIT_CMD_POWERFAIL:
            log_warn("initctl: UPS power failure — preparing shutdown");
            /* Give some time before powering off */
            g_state.reboot_cmd = TINIT_CMD_POWEROFF;
            g_state.running    = 0;
            break;

        case INIT_CMD_POWERFAILNOW:
            log_warn("initctl: UPS power failing NOW — immediate poweroff");
            g_state.reboot_cmd = TINIT_CMD_POWEROFF;
            g_state.running    = 0;
            break;

        case INIT_CMD_POWEROK:
            log_info("initctl: UPS power restored — cancelling shutdown");
            /* If we haven't started shutting down yet, cancel */
            if (!g_state.running) {
                g_state.running    = 1;
                g_state.reboot_cmd = TINIT_CMD_NONE;
            }
            break;

        case INIT_CMD_SETENV:
            /* Ignored — we don't implement dynamic env propagation */
            log_info("initctl: SETENV '%s' (ignored)", req.data.e.var);
            break;

        case INIT_CMD_START:
        case INIT_CMD_SETCONS:
            /* Silently ignore */
            break;

        default:
            log_warn("initctl: unknown cmd %d", req.cmd);
            break;
        }
    }
}

/* ------------------------------------------------------------------ */

void initctl_close(void)
{
    if (initctl_fd >= 0) {
        close(initctl_fd);
        initctl_fd = -1;
    }
    /* Leave the FIFO on disk — other tools may still try to open it */
}
