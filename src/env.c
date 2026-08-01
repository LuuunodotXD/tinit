/*
 * env.c — /etc/environment loader for tinit
 *
 * Portability:
 *   setenv(3)      — POSIX.1-2001, available on glibc/musl/uclibc
 *   fgets()        — C89
 *   opendir/readdir — POSIX
 *   qsort          — C89
 *   extern char **environ — C99 / POSIX, available everywhere
 *
 * No shell expansion: we intentionally do NOT expand $VAR or `cmd`.
 * /etc/environment is meant to be a simple static key=value file.
 */

#include <sys/types.h>
#include <dirent.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>

#include "tinit.h"
#include "log.h"
#include "env.h"

/* ------------------------------------------------------------------ */
/* Strip leading whitespace                                            */
/* ------------------------------------------------------------------ */
static char *ltrim(char *s)
{
    while (*s == ' ' || *s == '\t') s++;
    return s;
}

/* Strip trailing whitespace in-place */
static void rtrim(char *s)
{
    size_t n = strlen(s);
    while (n > 0 && (s[n-1] == '\n' || s[n-1] == '\r' ||
                     s[n-1] == ' '  || s[n-1] == '\t'))
        s[--n] = '\0';
}

/* ------------------------------------------------------------------ */
/* Validate environment variable name: [A-Za-z_][A-Za-z0-9_]*         */
/* ------------------------------------------------------------------ */
static int valid_key(const char *k)
{
    if (!*k) return 0;
    if (!isalpha((unsigned char)*k) && *k != '_') return 0;
    k++;
    while (*k) {
        if (!isalnum((unsigned char)*k) && *k != '_') return 0;
        k++;
    }
    return 1;
}

/* ------------------------------------------------------------------ */
/* Strip optional surrounding quotes from value (in-place)             */
/* Handles:  "value"  'value'  (single level only)                    */
/* ------------------------------------------------------------------ */
static char *unquote(char *s)
{
    size_t n = strlen(s);
    if (n >= 2) {
        if ((s[0] == '"'  && s[n-1] == '"') ||
            (s[0] == '\'' && s[n-1] == '\'')) {
            s[n-1] = '\0';
            s++;
        }
    }
    return s;
}

/* ------------------------------------------------------------------ */

int env_set(const char *keyval, int overwrite)
{
    char  buf[MAX_LINE];
    char *key, *val, *eq;

    strncpy(buf, keyval, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    rtrim(buf);

    key = ltrim(buf);

    /* Accept (and skip) optional "export " prefix */
    if (strncmp(key, "export ", 7) == 0)
        key = ltrim(key + 7);
    else if (strncmp(key, "export\t", 7) == 0)
        key = ltrim(key + 7);

    eq = strchr(key, '=');
    if (!eq) {
        /* Bare name with no value — set to empty string */
        if (!valid_key(key)) {
            log_warn("env: invalid key '%s'", key);
            return -1;
        }
        return setenv(key, "", overwrite);
    }

    *eq = '\0';
    val = unquote(eq + 1);

    if (!valid_key(key)) {
        log_warn("env: invalid key '%s'", key);
        return -1;
    }

    return setenv(key, val, overwrite);
}

/* ------------------------------------------------------------------ */

int env_load_file(const char *path, int overwrite)
{
    FILE *f;
    char  line[MAX_LINE];
    int   lineno  = 0;
    int   applied = 0;

    f = fopen(path, "r");
    if (!f) {
        if (errno != ENOENT)
            log_warn("env: open %s: %s", path, strerror(errno));
        return -1;
    }

    while (fgets(line, sizeof(line), f)) {
        char *s;
        lineno++;

        s = ltrim(line);
        rtrim(s);

        /* Skip comments and blank lines */
        if (*s == '\0' || *s == '#' || *s == ';')
            continue;

        if (env_set(s, overwrite) == 0)
            applied++;
        else
            log_warn("env: %s:%d: failed to set '%s'", path, lineno, s);
    }

    fclose(f);
    if (applied > 0)
        log_info("env: loaded %d variables from %s", applied, path);
    return applied;
}

/* ------------------------------------------------------------------ */
/* Directory loader — same pattern as sysctl_load_dir                 */
/* ------------------------------------------------------------------ */
#define MAX_DIR_ENTRIES 64

static int cmp_str(const void *a, const void *b)
{
    return strcmp(*(const char *const *)a, *(const char *const *)b);
}

int env_load_dir(const char *dirpath, int overwrite)
{
    DIR           *d;
    struct dirent *ent;
    char          *names[MAX_DIR_ENTRIES];
    int            count = 0;
    int            total = 0;
    int            i;

    d = opendir(dirpath);
    if (!d) {
        if (errno != ENOENT)
            log_warn("env: opendir %s: %s", dirpath, strerror(errno));
        return 0;
    }

    while ((ent = readdir(d)) != NULL && count < MAX_DIR_ENTRIES) {
        size_t nlen  = strlen(ent->d_name);
        const char *suffix = ".conf";
        size_t slen  = strlen(suffix);

        if (nlen <= slen) continue;
        if (strcmp(ent->d_name + nlen - slen, suffix) != 0) continue;
        if (ent->d_name[0] == '.') continue;

        names[count] = strdup(ent->d_name);
        if (names[count]) count++;
    }
    closedir(d);

    qsort(names, (size_t)count, sizeof(char *), cmp_str);

    for (i = 0; i < count; i++) {
        char fullpath[512];
        int  r;

        snprintf(fullpath, sizeof(fullpath), "%s/%s", dirpath, names[i]);
        r = env_load_file(fullpath, overwrite);
        if (r > 0) total += r;
        free(names[i]);
    }

    return total;
}

/* ------------------------------------------------------------------ */

void env_load_all(void)
{
    /*
     * Load order:
     *   /etc/environment.d/GLOB.conf  — package/vendor defaults (low priority)
     *   /etc/environment           — admin config (high priority, overrides)
     *
     * Within each file, later entries for the same key replace earlier ones
     * (setenv overwrite=1).  But /etc/environment DOES override .d/ files.
     */
    env_load_dir("/etc/environment.d", 1);
    env_load_file("/etc/environment", 1);

    /*
     * Set sensible defaults for variables not already defined.
     * These use overwrite=0, so they don't clobber what was in the files.
     */
    setenv("PATH",    "/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin", 0);
    setenv("TERM",    "linux",   0);
    setenv("HOME",    "/root",   0);
    setenv("SHELL",   SHELL_PATH, 0);
    setenv("LANG",    "C.UTF-8", 0);
}

/* ------------------------------------------------------------------ */

void env_dump(void)
{
    extern char **environ;
    char **e;
    log_info("env: current environment:");
    for (e = environ; e && *e; e++)
        log_info("  %s", *e);
}
