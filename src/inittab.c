/*
 * inittab.c — busybox-compatible /etc/inittab parser
 *
 * Format:  id:runlevels:action:process
 * Example: tty1::respawn:/sbin/getty -L tty1 115200 vt100
 *
 * Portability: uses fgets() (C89), strtok_r() (POSIX), no getline().
 */


#include <stdio.h>
#include <string.h>
#include <strings.h>   /* strcasecmp — POSIX.1-2008, <strings.h> */
#include <stdlib.h>
#include <errno.h>

#include "tinit.h"
#include "log.h"
#include "inittab.h"

/* ------------------------------------------------------------------ */

static const struct {
    const char *name;
    action_t    val;
} action_table[] = {
    { "sysinit",    A_SYSINIT    },
    { "boot",       A_BOOT       },
    { "bootwait",   A_BOOTWAIT   },
    { "wait",       A_WAIT       },
    { "once",       A_ONCE       },
    { "respawn",    A_RESPAWN    },
    { "askfirst",   A_ASKFIRST   },
    { "ctrlaltdel", A_CTRLALTDEL },
    { "shutdown",   A_SHUTDOWN   },
    { "restart",    A_RESTART    },
    { NULL, A_UNKNOWN }
};

action_t inittab_parse_action(const char *s)
{
    int i;
    for (i = 0; action_table[i].name != NULL; i++) {
        if (strcasecmp(action_table[i].name, s) == 0)
            return action_table[i].val;
    }
    return A_UNKNOWN;
}

/* ------------------------------------------------------------------ */
/* Strip trailing whitespace (in-place)                                */
/* ------------------------------------------------------------------ */
static void rtrim(char *s)
{
    size_t n = strlen(s);
    while (n > 0 && (s[n-1] == '\n' || s[n-1] == '\r' ||
                     s[n-1] == ' '  || s[n-1] == '\t'))
        s[--n] = '\0';
}

/* ------------------------------------------------------------------ */

int inittab_parse(const char *path,
                  inittab_entry_t *entries,
                  int max_entries)
{
    FILE *f;
    char  line[MAX_LINE];
    int   lineno = 0;
    int   count  = 0;

    f = fopen(path, "r");
    if (!f) {
        log_warn("cannot open %s: %s", path, strerror(errno));
        return -1;
    }

    while (fgets(line, sizeof(line), f) != NULL) {
        char *id, *runlevels, *action_str, *process;
        char *saveptr = NULL;
        inittab_entry_t *e;

        lineno++;

        rtrim(line);

        /* Skip blank lines and comments */
        if (line[0] == '\0' || line[0] == '#')
            continue;

        if (count >= max_entries) {
            log_warn("inittab: too many entries (max %d)", max_entries);
            break;
        }

        /* Split on ':' — up to 4 fields */
        id         = strtok_r(line,  ":", &saveptr);
        runlevels  = strtok_r(NULL,  ":", &saveptr);
        action_str = strtok_r(NULL,  ":", &saveptr);
        /* process: remainder of line (may contain ':') */
        process    = strtok_r(NULL,  "\n", &saveptr);

        if (!id || !runlevels || !action_str) {
            log_warn("inittab line %d: malformed, skipping", lineno);
            continue;
        }

        e = &entries[count];
        memset(e, 0, sizeof(*e));

        strncpy(e->id,       id,        sizeof(e->id)       - 1);
        strncpy(e->runlevels, runlevels, sizeof(e->runlevels) - 1);
        e->action = inittab_parse_action(action_str);

        if (e->action == A_UNKNOWN) {
            log_warn("inittab line %d: unknown action '%s'", lineno, action_str);
            continue;
        }

        if (process && *process)
            strncpy(e->process, process, sizeof(e->process) - 1);
        else
            e->process[0] = '\0';

        e->pid    = 0;
        e->exited = 0;
        count++;
    }

    fclose(f);
    log_info("parsed %d entries from %s", count, path);
    return count;
}

/* ------------------------------------------------------------------ */
/* Built-in default: spawn a shell on /dev/console if no inittab      */
/* ------------------------------------------------------------------ */
int inittab_default(inittab_entry_t *entries, int max_entries)
{
    inittab_entry_t *e;
    int n = 0;

    if (max_entries < 2) return 0;

    /* Mount essential filesystems via rcS if it exists */
    e = &entries[n++];
    memset(e, 0, sizeof(*e));
    strncpy(e->id, "rc", sizeof(e->id) - 1);
    e->action = A_SYSINIT;
    strncpy(e->process, INITSCRIPT_PATH, sizeof(e->process) - 1);

    /* Rescue shell on console */
    e = &entries[n++];
    memset(e, 0, sizeof(*e));
    strncpy(e->id, "con", sizeof(e->id) - 1);
    e->action = A_RESPAWN;
    strncpy(e->process, SHELL_PATH, sizeof(e->process) - 1);

    log_info("using built-in default inittab (%d entries)", n);
    return n;
}

/* ------------------------------------------------------------------ */

static const char *action_name(action_t a)
{
    int i;
    for (i = 0; action_table[i].name != NULL; i++)
        if (action_table[i].val == a)
            return action_table[i].name;
    return "unknown";
}

void inittab_dump(const inittab_entry_t *entries, int n)
{
    int i;
    log_info("--- inittab dump (%d entries) ---", n);
    for (i = 0; i < n; i++) {
        const inittab_entry_t *e = &entries[i];
        log_info("  [%d] id=%-4s action=%-12s cmd='%s'",
                 i, e->id, action_name(e->action), e->process);
    }
    log_info("--- end inittab ---");
}
