#ifndef COMPAT_H
#define COMPAT_H

/*
 * compat.h — portability shims for lean libc targets
 *
 * Injected into every translation unit via Makefile's -include flag.
 * Targets: glibc · musl · uclibc · dietlibc
 *
 * Design rules (learned the hard way):
 *
 *   1. Never pre-define anything that a system header also defines,
 *      unless guarded by __dietlibc__.  Reason: the -include flag
 *      processes this file BEFORE the source file's own #includes,
 *      so any macro we define here may conflict when the system
 *      header is included later.
 *
 *      Concrete example: glibc's <sys/mount.h> defines MNT_DETACH as
 *      both an enum member AND "#define MNT_DETACH MNT_DETACH".
 *      If we pre-define "#define MNT_DETACH 2" in this file, glibc's
 *      later redefinition causes "expected identifier before numeric
 *      constant" — a confusing error with no obvious cause.
 *
 *   2. Shims that are purely additive (new function names like
 *      tinit_basename, tinit_strtok_r) are always safe.
 *
 *   3. For dietlibc-specific fixes, guard with #ifdef __dietlibc__.
 *      The diet wrapper defines __dietlibc__ automatically.
 *
 * ── Shims provided ───────────────────────────────────────────────
 *
 *  tinit_basename()   Inline basename; replaces libgen.h on all targets.
 *                     Source files use TINIT_BASENAME() macro.
 *
 *  tinit_strtok_r()   Private re-entrant tokenizer; source files call
 *                     tinit_strtok_r() instead of strtok_r().
 *
 *  [dietlibc only]
 *  umount2()          dietlibc has umount() but not umount2().
 *                     Provided via syscall(SYS_umount2).
 *  MNT_DETACH         Not in dietlibc's <sys/mount.h>; defined here.
 *  strcasecmp()       In <string.h> on dietlibc, not <strings.h>.
 *                     We skip the <strings.h> include on dietlibc.
 */

/* ── Base headers (safe on all targets) ─────────────────────────── */
#include <sys/types.h>
#include <sys/stat.h>   /* struct stat, makedev() on all targets     */
#include <string.h>     /* memcpy, strdup, strcasecmp (dietlibc)     */
#include <stdlib.h>     /* malloc, free, qsort                       */
#include <errno.h>
#include <unistd.h>

/* ── strcasecmp lives in <strings.h> on glibc/musl/uclibc ──────── */
#ifndef __dietlibc__
#  include <strings.h>
#  include <sys/sysmacros.h>  /* makedev() — inline fn on glibc ≥ 2.27 */
#endif

/* ── dietlibc-specific fixes ─────────────────────────────────────── */
#ifdef __dietlibc__

#  include <sys/syscall.h>

   /* MNT_DETACH: dietlibc's <sys/mount.h> may not define this */
#  ifndef MNT_DETACH
#    define MNT_DETACH 2
#  endif

   /* umount2(): not in dietlibc; use Linux syscall directly */
#  ifndef SYS_umount2
     /* Fallback — kernel defines this per-arch in <asm/unistd.h>
      * which dietlibc includes via sys/syscall.h.  If still missing,
      * hardcode the most common value (166 on x86_64, 52 on x86).  */
#    if defined(__x86_64__)
#      define SYS_umount2 166
#    elif defined(__i386__)
#      define SYS_umount2 52
#    elif defined(__aarch64__)
#      define SYS_umount2 39
#    elif defined(__arm__)
#      define SYS_umount2 52
#    elif defined(__powerpc__)
#      define SYS_umount2 22
#    elif defined(__s390__)
#      define SYS_umount2 22
#    else
#      define SYS_umount2 22   /* conservative guess */
#    endif
#  endif

static inline int umount2(const char *target, int flags)
{
    return (int)syscall(SYS_umount2, target, flags);
}

#endif /* __dietlibc__ */

/* ── tinit_basename() — no libgen.h dependency ───────────────────── */
/*
 * Returns pointer to the last path component.
 * Does NOT modify the string. Thread-safe. No static buffer.
 * Use TINIT_BASENAME(s) in source files.
 */
static inline const char *tinit_basename(const char *str)
{
    const char *last = str;
    const char *p;
    if (!str || !*str) return ".";
    for (p = str; *p; p++)
        if (p[0] == '/' && p[1] != '\0')
            last = p + 1;
    return last;
}
#define TINIT_BASENAME(s) tinit_basename(s)

/* ── tinit_strtok_r() — our private re-entrant tokenizer ─────────── */
/*
 * Provided unconditionally so source files never depend on the libc
 * version (avoids issues with old uclibc / stripped dietlibc builds).
 * The compiler dead-strips it in translation units that don't use it.
 */
static inline char *tinit_strtok_r(char *str, const char *delim,
                                   char **saveptr)
{
    char *s, *end;

    s = str ? str : *saveptr;
    if (!s) { *saveptr = NULL; return NULL; }

    while (*s && strchr(delim, (unsigned char)*s))   /* skip delimiters */
        s++;
    if (!*s) { *saveptr = NULL; return NULL; }

    for (end = s; *end && !strchr(delim, (unsigned char)*end); end++)
        ;                                             /* find token end  */

    if (*end) { *end = '\0'; *saveptr = end + 1; }
    else       { *saveptr = NULL; }

    return s;
}

#endif /* COMPAT_H */
