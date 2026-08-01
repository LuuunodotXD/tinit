/*
 * cmdline.c — kernel /proc/cmdline parser for tinit
 *
 * /proc/cmdline contains the kernel command line as a single line:
 *   root=/dev/sda1 rw console=ttyS0,115200 quiet tinit.loglevel=2
 *
 * Tokens are space-separated.  Each token is either:
 *   - A bare flag:  "quiet", "rw", "tinit.debug"
 *   - A key=value:  "console=ttyS0,115200", "root=/dev/sda1"
 *
 * We make a single pass, splitting on whitespace and filling g_cmdline.
 */


#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <stdio.h>

#include "tinit.h"
#include "log.h"
#include "cmdline.h"

/* ------------------------------------------------------------------ */
/* Global instance                                                     */
/* ------------------------------------------------------------------ */
cmdline_t g_cmdline;

/* ------------------------------------------------------------------ */
/* Read /proc/cmdline into buf, returns bytes read or -1              */
/* ------------------------------------------------------------------ */
static ssize_t read_cmdline(char *buf, size_t size)
{
    int     fd;
    ssize_t n;

    fd = open("/proc/cmdline", O_RDONLY);
    if (fd < 0) {
        log_warn("cmdline: cannot open /proc/cmdline: %s", strerror(errno));
        return -1;
    }

    do { n = read(fd, buf, size - 1); } while (n < 0 && errno == EINTR);
    close(fd);

    if (n < 0) {
        log_warn("cmdline: read: %s", strerror(errno));
        return -1;
    }

    /* Strip trailing newline */
    while (n > 0 && (buf[n-1] == '\n' || buf[n-1] == '\r'))
        n--;
    buf[n] = '\0';
    return n;
}

/* ------------------------------------------------------------------ */
/* Process one token (either "flag" or "key=value")                   */
/* ------------------------------------------------------------------ */
static void process_token(const char *tok)
{
    const char *eq = strchr(tok, '=');
    const char *val = eq ? eq + 1 : NULL;
    size_t keylen  = eq ? (size_t)(eq - tok) : strlen(tok);

#define KEY_IS(s) (keylen == sizeof(s)-1 && strncmp(tok, (s), keylen) == 0)

    if (KEY_IS("quiet")) {
        g_cmdline.quiet = 1;
        if (g_cmdline.loglevel < TINIT_LOG_QUIET)
            g_cmdline.loglevel = TINIT_LOG_QUIET;

    } else if (KEY_IS("debug")) {
        g_cmdline.debug    = 1;
        g_cmdline.loglevel = TINIT_LOG_DEBUG;

    } else if (KEY_IS("console") && val) {
        /* Take the last console= (kernel uses the last one too) */
        strncpy(g_cmdline.console, val, sizeof(g_cmdline.console) - 1);

    } else if (KEY_IS("init") && val) {
        strncpy(g_cmdline.init, val, sizeof(g_cmdline.init) - 1);

    } else if (KEY_IS("tinit.debug")) {
        g_cmdline.debug    = 1;
        g_cmdline.loglevel = TINIT_LOG_DEBUG;

    } else if (KEY_IS("tinit.loglevel") && val) {
        g_cmdline.loglevel = atoi(val);
        if (g_cmdline.loglevel < TINIT_LOG_DEBUG)
            g_cmdline.loglevel = TINIT_LOG_DEBUG;
        if (g_cmdline.loglevel > TINIT_LOG_QUIET)
            g_cmdline.loglevel = TINIT_LOG_QUIET;
    }

#undef KEY_IS
}

/* ------------------------------------------------------------------ */

void cmdline_parse(void)
{
    char   work[CMDLINE_MAX]; /* mutable copy for strtok_r */
    char  *saveptr = NULL;
    char  *tok;
    ssize_t n;

    memset(&g_cmdline, 0, sizeof(g_cmdline));
    g_cmdline.loglevel = TINIT_LOG_INFO;   /* default */

    n = read_cmdline(g_cmdline.raw, sizeof(g_cmdline.raw));
    if (n <= 0)
        return;

    strncpy(work, g_cmdline.raw, sizeof(work) - 1);
    work[sizeof(work) - 1] = '\0';

    tok = strtok_r(work, " \t", &saveptr);
    while (tok) {
        process_token(tok);
        tok = strtok_r(NULL, " \t", &saveptr);
    }

    log_info("cmdline: quiet=%d debug=%d loglevel=%d console='%s' init='%s'",
             g_cmdline.quiet, g_cmdline.debug, g_cmdline.loglevel,
             g_cmdline.console, g_cmdline.init);
}

/* ------------------------------------------------------------------ */
/* Generic lookup in the raw string                                    */
/* ------------------------------------------------------------------ */
const char *cmdline_get(const char *key)
{
    size_t klen = strlen(key);
    char  *p    = g_cmdline.raw;

    while (*p) {
        /* Skip whitespace */
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;

        if (strncmp(p, key, klen) == 0 && p[klen] == '=') {
            /* Found "key=..." — return pointer to value */
            return p + klen + 1;
        }

        /* Advance to next token */
        while (*p && *p != ' ' && *p != '\t') p++;
    }
    return NULL;
}

int cmdline_has(const char *key)
{
    size_t klen = strlen(key);
    char  *p    = g_cmdline.raw;

    while (*p) {
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;

        if (strncmp(p, key, klen) == 0 &&
            (p[klen] == '\0' || p[klen] == ' ' ||
             p[klen] == '\t' || p[klen] == '='))
            return 1;

        while (*p && *p != ' ' && *p != '\t') p++;
    }
    return 0;
}

/* ------------------------------------------------------------------ */

void cmdline_dump(void)
{
    log_info("cmdline raw: '%s'", g_cmdline.raw);
}
