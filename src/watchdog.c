/*
 * watchdog.c — hardware watchdog keepalive for tinit
 *
 * Portability notes:
 *   - <linux/watchdog.h> defines WDIOC_* ioctls and WDIOF_* flags.
 *     It may be missing in some minimal sysroots (old uclibc, custom
 *     musl with stripped kernel headers).  We guard with #ifdef and
 *     fall back to write-only keepalive when it's absent.
 *   - The write-based keepalive (write any byte) is always available
 *     and works on every Linux watchdog driver.
 *   - Magic close ('V') is optional: drivers that don't support
 *     WDIOF_MAGICCLOSE ignore it but don't error either.
 */

#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

/* Kernel watchdog ioctl header — may be absent in lean sysroots */
#ifdef __has_include
#  if __has_include(<linux/watchdog.h>)
#    include <linux/watchdog.h>
#    define HAVE_LINUX_WATCHDOG_H 1
#  endif
#else
/* Fallback for compilers without __has_include (old GCC) */
#  ifdef __linux__
#    include <linux/watchdog.h>
#    define HAVE_LINUX_WATCHDOG_H 1
#  endif
#endif

#include "tinit.h"
#include "log.h"
#include "watchdog.h"

int watchdog_fd = -1;
static int kick_counter = 0;   /* seconds since last kick */

/* ------------------------------------------------------------------ */

int watchdog_open(void)
{
    struct stat st;

    if (stat(WATCHDOG_DEV, &st) < 0) {
        log_info("watchdog: %s not found — no hardware watchdog", WATCHDOG_DEV);
        return -1;
    }

    /*
     * Opening /dev/watchdog STARTS the timer immediately.
     * From this point on we must either kick regularly or close with 'V'.
     */
    watchdog_fd = open(WATCHDOG_DEV, O_WRONLY | O_CLOEXEC);
    if (watchdog_fd < 0) {
        log_warn("watchdog: open %s: %s", WATCHDOG_DEV, strerror(errno));
        return -1;
    }

#ifdef HAVE_LINUX_WATCHDOG_H
    /* Query driver capabilities */
    {
        struct watchdog_info info;
        if (ioctl(watchdog_fd, WDIOC_GETSUPPORT, &info) == 0) {
            log_info("watchdog: driver='%s' version=%u flags=0x%08x",
                     info.identity,
                     info.firmware_version,
                     info.options);
        }
    }

    /* Try to set hardware timeout */
    {
        int timeout = WATCHDOG_TIMEOUT;
        if (ioctl(watchdog_fd, WDIOC_SETTIMEOUT, &timeout) == 0) {
            log_info("watchdog: timeout set to %d seconds (actual=%d)",
                     WATCHDOG_TIMEOUT, timeout);
        } else {
            /* Read what timeout is actually in effect */
            if (ioctl(watchdog_fd, WDIOC_GETTIMEOUT, &timeout) == 0)
                log_info("watchdog: driver timeout=%d s (set unavailable)",
                         timeout);
            else
                log_info("watchdog: timeout ioctl unavailable, using driver default");
        }
    }
#else
    log_info("watchdog: opened %s (no ioctl support in headers)", WATCHDOG_DEV);
#endif

    kick_counter = 0;
    log_info("watchdog: started, kicking every %d s", WATCHDOG_KICK_INTERVAL);
    return watchdog_fd;
}

/* ------------------------------------------------------------------ */

void watchdog_tick(void)
{
    if (watchdog_fd < 0)
        return;

    kick_counter++;
    if (kick_counter < WATCHDOG_KICK_INTERVAL)
        return;

    kick_counter = 0;

    /*
     * Any write resets the timer.  We use WDIOC_KEEPALIVE if available
     * (cleaner), otherwise fall back to writing a byte.
     */
#ifdef HAVE_LINUX_WATCHDOG_H
    if (ioctl(watchdog_fd, WDIOC_KEEPALIVE, 0) == 0)
        return;
    /* ioctl failed — fall through to write */
#endif
    {
        ssize_t n;
        do { n = write(watchdog_fd, "1", 1); } while (n < 0 && errno == EINTR);
        if (n < 0)
            log_warn("watchdog: kick failed: %s", strerror(errno));
    }
}

/* ------------------------------------------------------------------ */

void watchdog_close(void)
{
    if (watchdog_fd < 0)
        return;

    /*
     * Write magic 'V' before close.
     * Drivers with WDIOF_MAGICCLOSE will NOT fire after this.
     * Drivers without this flag will fire — but that's intentional
     * for a hard reset path.  On orderly shutdown we don't want
     * a stray reset, so we write 'V' and hope the driver supports it.
     */
    {
        ssize_t n;
        do { n = write(watchdog_fd, "V", 1); } while (n < 0 && errno == EINTR);
        if (n < 0)
            log_warn("watchdog: magic close write failed: %s", strerror(errno));
        else
            log_info("watchdog: magic close sent");
    }

    close(watchdog_fd);
    watchdog_fd = -1;
}
