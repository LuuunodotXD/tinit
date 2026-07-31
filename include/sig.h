#ifndef SIG_H
#define SIG_H

/*
 * Signal handling for PID 1.
 *
 * PID 1 has special kernel rules:
 *  - Signals whose default action is "terminate" are IGNORED unless
 *    a handler is explicitly installed.
 *  - SIGKILL and SIGSTOP cannot be caught, but the kernel will not
 *    deliver them to PID 1 (they are quietly discarded).
 *
 * We use a self-pipe to make signal delivery select()-able from the
 * main loop, which is safe with uclibc/musl (no signalfd needed).
 *
 * Signals we handle:
 *   SIGCHLD  — a child process exited, reap it
 *   SIGINT   — Ctrl+Alt+Del (kernel sends this to PID 1)
 *   SIGTERM  — halt system
 *   SIGUSR1  — halt system (alternative)
 *   SIGUSR2  — power off
 *   SIGHUP   — reload inittab
 */

/* Read end of signal pipe — poll this in the main loop */
extern int sig_pipe_r;

/* Install all handlers and create the self-pipe */
void sig_init(void);

/* Block all catchable signals (call before fork so children inherit) */
void sig_block_all(void);

/* Restore default/empty mask (call in child after fork) */
void sig_unblock_all(void);

#endif /* SIG_H */
