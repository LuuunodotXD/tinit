#ifndef ENV_H
#define ENV_H

/*
 * env.h — environment variable loader for tinit
 *
 * Reads /etc/environment (and optionally /etc/environment.d/GLOB.conf)
 * and exports the variables so all child processes inherit them.
 *
 * File format (/etc/environment):
 *
 *   # comment
 *   KEY=value
 *   QUOTED_KEY="value with spaces"
 *   SINGLE_QUOTED='value'
 *   PATH=/usr/local/bin:/usr/bin:/bin
 *
 * Rules:
 *   - One KEY=VALUE per line (no shell expansion, no substitution)
 *   - Lines starting with '#' are comments
 *   - Blank lines are ignored
 *   - Optional surrounding quotes are stripped from value
 *   - Keys must match [A-Za-z_][A-Za-z0-9_]* (others are warned & skipped)
 *   - Values may contain any printable character
 *
 * The "export" keyword is silently accepted (for bash compat) but ignored.
 *
 * After env_load_all():
 *   - All exported variables are set via setenv(3)
 *   - All child processes (getty, services, shells) inherit them
 *   - Existing variables are NOT overridden (tinit's own env takes precedence)
 *     unless env_load_file() is called with overwrite=1
 */

/* Load one file.  overwrite=1 replaces existing env vars.
 * Returns number of variables set, or -1 on open error.          */
int env_load_file(const char *path, int overwrite);

/* Load /etc/environment.d/GLOB.conf in sorted order.                */
int env_load_dir(const char *dirpath, int overwrite);

/*
 * Load the full standard environment:
 *   1. /etc/environment.d/GLOB.conf  (vendor/package defaults)
 *   2. /etc/environment            (local admin config, overrides)
 *
 * Call once during sysinit, before spawning any children.
 */
void env_load_all(void);

/* Set a single variable (thin wrapper around setenv).
 * Parses "KEY=VALUE" or just "KEY" (sets to "").                 */
int env_set(const char *keyval, int overwrite);

/* Print all current environment variables to console (debug).    */
void env_dump(void);

#endif /* ENV_H */
