#ifndef LOG_H
#define LOG_H

#include "tinit.h"

/*
 * All output goes to /dev/console (opened as a raw fd).
 * We avoid stdio here on purpose:
 *  - stdio is not async-signal-safe
 *  - /dev/console may not be open on early boot
 *  - write() is always async-signal-safe
 */

void log_init(void);
void log_close(void);

/* loglevel-aware, timestamp-prefixed write to /dev/console */
void PRINTF_FMT(1,2) log_info (const char *fmt, ...);
void PRINTF_FMT(1,2) log_warn (const char *fmt, ...);
void PRINTF_FMT(1,2) log_error(const char *fmt, ...);

/* signal-safe: writes a literal string, no formatting */
void log_raw(const char *msg);

#endif /* LOG_H */
