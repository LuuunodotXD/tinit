/*
 * sysctl.c — /etc/sysctl.conf and /etc/sysctl.d/ loader for tinit
 *
 * Portability:
 *   opendir/readdir/closedir — POSIX, available on glibc/musl/uclibc
 *   qsort                   — C89
 *   No glob(), no fnmatch() needed
 *   fgets() for line reading — C89
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "tinit.h"
#include "log.h"
#include "sysctl.h"

#define PROC_SYS      "/proc/sys/"
#define MAX_KEY       256
#define MAX_VAL       512
#define MAX_PROC_PATH (sizeof(PROC_SYS) + MAX_KEY + 1)

/* ------------------------------------------------------------------ */
/* Translate "net.ipv4.ip_forward" → "/proc/sys/net/ipv4/ip_forward"  */
/* Absolute paths ("/proc/sys/...") are used as-is.                    */
/* ------------------------------------------------------------------ */
static void key_to_path(const char *key, char *out, size_t outsz)
{
    size_t i;

    if (key[0] == '/') {
        /* Already an absolute path */
        strncpy(out, key, outsz - 1);
        out[outsz - 1] = '\0';
        return;
    }

    strncpy(out, PROC_SYS, outsz - 1);
    out[outsz - 1] = '\0';

    /* Append key, replacing '.' with '/' */
    i = strlen(out);
    while (*key && i < outsz - 1) {
        out[i++] = (*key == '.') ? '/' : *key;
        key++;
    }
    out[i] = '\0';
}

/* ------------------------------------------------------------------ */
/* Strip leading and trailing whitespace (in-place)                    */
/* ------------------------------------------------------------------ */
static char *trim(char *s)
{
    char *end;

    while (*s == ' ' || *s == '\t') s++;
    if (*s == '\0') return s;

    end = s + strlen(s) - 1;
    while (end > s && (*end == ' ' || *end == '\t' ||
                       *end == '\n' || *end == '\r'))
        end--;
    *(end + 1) = '\0';
    return s;
}

/* ------------------------------------------------------------------ */

int sysctl_set(const char *key, const char *value)
{
    char    path[MAX_PROC_PATH];
    int     fd;
    ssize_t n;
    size_t  vlen;
    int     ignore_error = 0;

    /* Leading '-' means "ignore errors for this key" */
    if (key[0] == '-') {
        ignore_error = 1;
        key++;
    }

    key_to_path(key, path, sizeof(path));
    vlen = strlen(value);

    fd = open(path, O_WRONLY);
    if (fd < 0) {
        if (!ignore_error)
            log_warn("sysctl: open %s: %s", path, strerror(errno));
        return -1;
    }

    do { n = write(fd, value, vlen); } while (n < 0 && errno == EINTR);

    if (n < 0) {
        if (!ignore_error)
            log_warn("sysctl: write %s=%s: %s", key, value, strerror(errno));
        close(fd);
        return -1;
    }

    /* Trailing newline helps some drivers */
    if (vlen == 0 || value[vlen-1] != '\n') {
        do { n = write(fd, "\n", 1); } while (n < 0 && errno == EINTR);
    }

    close(fd);
    return 0;
}

/* ------------------------------------------------------------------ */

int sysctl_load_file(const char *path)
{
    FILE *f;
    char  line[MAX_KEY + MAX_VAL + 4];
    int   lineno  = 0;
    int   applied = 0;

    f = fopen(path, "r");
    if (!f) {
        if (errno != ENOENT)
            log_warn("sysctl: open %s: %s", path, strerror(errno));
        return -1;
    }

    while (fgets(line, sizeof(line), f)) {
        char *key, *val, *eq;

        lineno++;

        key = trim(line);

        /* Skip blank lines and comments */
        if (key[0] == '\0' || key[0] == '#' || key[0] == ';')
            continue;

        /* Split on '=' */
        eq = strchr(key, '=');
        if (!eq) {
            log_warn("sysctl: %s:%d: no '=' in '%s'", path, lineno, key);
            continue;
        }

        *eq = '\0';
        val = trim(eq + 1);
        key = trim(key);   /* trim key after splitting */

        if (key[0] == '\0' || val == NULL) {
            log_warn("sysctl: %s:%d: empty key or value", path, lineno);
            continue;
        }

        if (sysctl_set(key, val) == 0)
            applied++;
    }

    fclose(f);
    if (applied > 0)
        log_info("sysctl: applied %d settings from %s", applied, path);
    return applied;
}

/* ------------------------------------------------------------------ */
/* Sort helper for qsort: compare dirent names                         */
/* ------------------------------------------------------------------ */
#define MAX_DIR_ENTRIES 128

static int cmp_str(const void *a, const void *b)
{
    return strcmp(*(const char *const *)a, *(const char *const *)b);
}

int sysctl_load_dir(const char *dirpath)
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
            log_warn("sysctl: opendir %s: %s", dirpath, strerror(errno));
        return 0;
    }

    /* Collect *.conf filenames */
    while ((ent = readdir(d)) != NULL && count < MAX_DIR_ENTRIES) {
        size_t nlen = strlen(ent->d_name);
        const char *suffix = ".conf";
        size_t slen = strlen(suffix);

        if (nlen <= slen)
            continue;
        if (strcmp(ent->d_name + nlen - slen, suffix) != 0)
            continue;
        if (ent->d_name[0] == '.')
            continue;

        names[count] = strdup(ent->d_name);
        if (names[count])
            count++;
    }
    closedir(d);

    /* Sort lexicographically (same order as systemd/procps) */
    qsort(names, (size_t)count, sizeof(char *), cmp_str);

    for (i = 0; i < count; i++) {
        char fullpath[512];
        int  r;

        snprintf(fullpath, sizeof(fullpath), "%s/%s", dirpath, names[i]);
        r = sysctl_load_file(fullpath);
        if (r > 0) total += r;
        free(names[i]);
    }

    return total;
}

/* ------------------------------------------------------------------ */

void sysctl_load_all(void)
{
    /*
     * Load order mirrors procps sysctl(8) and systemd:
     *   /etc/sysctl.conf           (legacy, highest user priority)
     *   /etc/sysctl.d/GLOB.conf
     *   /run/sysctl.d/GLOB.conf       (runtime overrides)
     *   /usr/lib/sysctl.d/GLOB.conf   (vendor defaults, lowest priority)
     *
     * Later files override earlier files for the same key
     * (last write to /proc/sys wins).
     */
    static const char * const dirs[] = {
        "/usr/lib/sysctl.d",
        "/run/sysctl.d",
        "/etc/sysctl.d",
        NULL
    };
    int i;

    /* Load vendor defaults first (lowest priority) */
    for (i = 0; dirs[i]; i++)
        sysctl_load_dir(dirs[i]);

    /* /etc/sysctl.conf last = highest priority */
    sysctl_load_file("/etc/sysctl.conf");
}
