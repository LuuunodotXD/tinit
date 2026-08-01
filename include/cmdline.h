#ifndef CMDLINE_H
#define CMDLINE_H

/*
 * cmdline.h — /proc/cmdline parser
 *
 * Reads kernel boot parameters and exposes them as:
 *   - Typed fields for well-known keys (console=, quiet, init=, ...)
 *   - Generic key/value lookup via cmdline_get() / cmdline_has()
 *
 * tinit-specific params use the prefix "tinit.":
 *   tinit.loglevel=3   suppress everything below level 3
 *   tinit.debug        alias for tinit.loglevel=0
 *   tinit.no_fstab     skip /etc/fstab mounting (not implemented here)
 */

#define CMDLINE_MAX      4096  /* /proc/cmdline is at most 4096 bytes      */
#define CMDLINE_MAX_ARGS 128   /* max individual tokens                     */
#define CMDLINE_CONSOLE  64    /* max length of console= value              */
#define CMDLINE_INIT     256   /* max length of init= path                  */

/* Log levels (mirrors syslog severity, but simplified) */
#define TINIT_LOG_DEBUG  0
#define TINIT_LOG_INFO   1
#define TINIT_LOG_WARN   2
#define TINIT_LOG_ERROR  3
#define TINIT_LOG_QUIET  4    /* nothing except fatal */

typedef struct {
    /* Known kernel parameters */
    char console[CMDLINE_CONSOLE]; /* console=ttyS0,115200  → "ttyS0,115200" */
    char init[CMDLINE_INIT];       /* init=/sbin/init       → "/sbin/init"   */
    int  quiet;                    /* "quiet" flag present?                   */
    int  debug;                    /* "debug" or "tinit.debug" flag?         */

    /* tinit-specific */
    int  loglevel;                 /* tinit.loglevel=N  (TINIT_LOG_*)        */

    /* Raw storage for generic lookup */
    char raw[CMDLINE_MAX];
} cmdline_t;

extern cmdline_t g_cmdline;

/* Parse /proc/cmdline and populate g_cmdline */
void cmdline_parse(void);

/*
 * Look up "key=value" → returns pointer to "value" inside g_cmdline.raw,
 * or NULL if not found.  The returned string is valid until the next
 * cmdline_parse() call.  Caller must NOT free it.
 */
const char *cmdline_get(const char *key);

/* Returns 1 if a bare flag (no "=") or a "key=..." is present */
int cmdline_has(const char *key);

/* Print all parsed tokens to console (debug) */
void cmdline_dump(void);

#endif /* CMDLINE_H */
