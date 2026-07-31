#ifndef PROCESS_H
#define PROCESS_H

#include "tinit.h"

/*
 * Process spawning and zombie reaping.
 *
 * spawn_entry()  — fork+exec one inittab entry, returns child PID
 * reap_children()— non-blocking waitpid loop; marks exited entries
 * run_and_wait() — run a command synchronously (for sysinit/wait)
 * exec_entry()   — called in child: set up fds, exec the command
 */

pid_t spawn_entry(inittab_entry_t *e);
void  reap_children(void);
int   run_and_wait(const char *cmd);

#endif /* PROCESS_H */
