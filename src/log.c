/*
 * log.c — console logging for tinit
 *
 * Uses write(2) directly so it is safe to call from signal handlers
 * (log_raw only) and does not depend on stdio buffering.
 */


#include <fcntl.h>
#include <unistd.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>   /* vsnprintf — not async-signal-safe, only in log_info/warn/error */

#include "tinit.h"
#include "log.h"

static int console_fd = -1;

/* ------------------------------------------------------------------ */

void log_init(void)
{
    /* Try to open the console; fall back to stderr */
    console_fd = open(CONSOLE_DEV, O_WRONLY | O_NOCTTY | O_CLOEXEC);
    if (console_fd < 0)
        console_fd = STDERR_FILENO;
}

void log_close(void)
{
    if (console_fd >= 0 && console_fd != STDERR_FILENO) {
        close(console_fd);
        console_fd = -1;
    }
}

/* ------------------------------------------------------------------ */
/* Internal writer — handles EINTR, partial writes                     */
/* ------------------------------------------------------------------ */
static void write_all(const char *buf, size_t len)
{
    int fd = (console_fd >= 0) ? console_fd : STDERR_FILENO;
    while (len > 0) {
        ssize_t n;
        do { n = write(fd, buf, len); } while (n < 0 && errno == EINTR);
        if (n <= 0) break;
        buf += (size_t)n;
        len -= (size_t)n;
    }
}

/* ------------------------------------------------------------------ */
/* Async-signal-safe: only write(), no vsnprintf                       */
/* ------------------------------------------------------------------ */
void log_raw(const char *msg)
{
    if (msg)
        write_all(msg, strlen(msg));
}

/* ------------------------------------------------------------------ */
/* Formatted logging (not signal-safe)                                 */
/* ------------------------------------------------------------------ */
static void log_vprintf(const char *prefix, const char *fmt, va_list ap)
{
    char buf[MAX_LINE * 2];
    int  n;

    /* Prefix: "tinit: [LEVEL] " */
    n = snprintf(buf, sizeof(buf), "tinit: %s", prefix);
    if (n < 0 || (size_t)n >= sizeof(buf))
        n = 0;

    {
        int r = vsnprintf(buf + n, sizeof(buf) - (size_t)n, fmt, ap);
        if (r > 0) n += r;
    }

    /* Ensure newline */
    if (n > 0 && buf[n - 1] != '\n' && (size_t)n < sizeof(buf) - 1) {
        buf[n++] = '\n';
    }

    write_all(buf, (size_t)n);
}

void log_info(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    log_vprintf("", fmt, ap);
    va_end(ap);
}

void log_warn(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    log_vprintf("[WARN] ", fmt, ap);
    va_end(ap);
}

void log_error(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    log_vprintf("[ERR] ", fmt, ap);
    va_end(ap);
}
