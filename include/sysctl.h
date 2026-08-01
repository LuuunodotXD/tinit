#ifndef SYSCTL_H
#define SYSCTL_H

/*
 * sysctl.h — /etc/sysctl.conf and /etc/sysctl.d/ loader
 *
 * Applies kernel parameters by writing values to /proc/sys/.
 * Compatible with the format used by procps sysctl(8).
 *
 * File format (/etc/sysctl.conf, /etc/sysctl.d/GLOB.conf):
 *
 *   # comment
 *   ; comment
 *   net.ipv4.ip_forward = 1
 *   kernel.hostname = mybox
 *   vm.swappiness=10
 *   -net.ipv6.conf.all.disable_ipv6 = 0   (leading '-' = ignore errors)
 *
 * Key translation:
 *   Replace '.' with '/' → /proc/sys/net/ipv4/ip_forward
 *   Keys beginning with '/' are used as-is (absolute /proc/sys path).
 *
 * Load order (same as systemd/procps):
 *   1. /etc/sysctl.conf
 *   2. /etc/sysctl.d/GLOB.conf  (lexicographic order)
 *   3. /run/sysctl.d/GLOB.conf
 *   4. /usr/lib/sysctl.d/GLOB.conf
 *
 * Later files override earlier ones (last-write-wins per key).
 */

/* Apply a single file.  Returns number of keys set, or -1 on open error. */
int sysctl_load_file(const char *path);

/* Apply a directory of *.conf files in sorted order. */
int sysctl_load_dir(const char *dirpath);

/*
 * Apply the full standard load order.
 * Call once during sysinit phase, before spawning services.
 */
void sysctl_load_all(void);

/* Set a single key=value pair programmatically (for cmdline overrides). */
int sysctl_set(const char *key, const char *value);

#endif /* SYSCTL_H */
