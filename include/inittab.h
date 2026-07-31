#ifndef INITTAB_H
#define INITTAB_H

#include "tinit.h"

/*
 * Busybox-compatible inittab parser.
 *
 * Format:
 *   id:runlevels:action:process
 *
 *   id        — up to 4-char identifier (used as console tty suffix)
 *   runlevels — ignored by tinit (we don't implement runlevels)
 *   action    — one of: sysinit wait once respawn askfirst
 *               ctrlaltdel shutdown restart boot bootwait
 *   process   — command to run (via /bin/sh -c "..." or parsed argv)
 *
 * Lines starting with '#' or blank lines are ignored.
 *
 * Returns the number of entries parsed, or -1 on error.
 */
int  inittab_parse(const char *path,
                   inittab_entry_t *entries,
                   int max_entries);

/* Load a built-in default table when /etc/inittab is missing */
int  inittab_default(inittab_entry_t *entries, int max_entries);

/* Convert action string to action_t */
action_t inittab_parse_action(const char *s);

/* Dump parsed entries to console (debug) */
void inittab_dump(const inittab_entry_t *entries, int n);

#endif /* INITTAB_H */
