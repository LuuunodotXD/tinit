/*
 * process.c — fork/exec and zombie reaping for tinit
 *
 * Portability notes:
 *  - execvp() instead of execvpe() (uclibc may lack execvpe)
 *  - environ is set before exec via explicit manipulation
 *  - strtok_r() instead of strtok() (re-entrant)
 *  - No getline() — use fgets() which is in C89
 */


#include <sys/types.h>
#include <sys/wait.h>
#include <sys/ioctl.h>      /* ioctl(), TIOCSCTTY */
#include <termios.h>        /* TIOCSCTTY on some libcs */
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "tinit.h"
#include "log.h"
#include "sig.h"
#include "process.h"

/* ------------------------------------------------------------------ */
/* Parse cmd string into argv[] for execvp.                            */
/* Simple whitespace split — no quoting/escaping.                      */
/* ------------------------------------------------------------------ */
static int split_args(char *cmd, char *argv[], int max_argv)
{
    char *saveptr = NULL;
    char *tok;
    int   n = 0;

    tok = strtok_r(cmd, " \t\n\r", &saveptr);
    while (tok && n < max_argv - 1) {
        argv[n++] = tok;
        tok = strtok_r(NULL, " \t\n\r", &saveptr);
    }
    argv[n] = NULL;
    return n;
}

/* ------------------------------------------------------------------ */
/* Child-side setup before exec                                        */
/* ------------------------------------------------------------------ */
static NORETURN void child_exec(const char *cmd, int askfirst)
{
    char  buf[MAX_LINE];
    char *argv[MAX_ARGS];
    int   fd, n;

    /* Unblock signals and restore defaults */
    sig_unblock_all();

    /* Become session leader + controlling terminal */
    setsid();

    /* Open /dev/console as stdin/stdout/stderr */
    fd = open(CONSOLE_DEV, O_RDWR | O_NOCTTY);
    if (fd < 0)
        fd = open(NULL_DEV, O_RDWR);

    if (fd >= 0) {
        /* Make this tty the controlling terminal */
        ioctl(fd, TIOCSCTTY, 0);

        dup2(fd, STDIN_FILENO);
        dup2(fd, STDOUT_FILENO);
        dup2(fd, STDERR_FILENO);
        if (fd > STDERR_FILENO)
            close(fd);
    }

    /* askfirst: prompt before starting */
    if (askfirst) {
        const char *prompt = "\nPress Enter to activate this console\n";
        (void)write(STDOUT_FILENO, prompt, strlen(prompt));
        {
            char ch;
            while (read(STDIN_FILENO, &ch, 1) == 1 && ch != '\n')
                ;
        }
    }

    /* Build argv */
    strncpy(buf, cmd, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    n = split_args(buf, argv, MAX_ARGS);

    if (n == 0) {
        /* Empty command — run a shell */
        argv[0] = SHELL_PATH;
        argv[1] = NULL;
        execvp(SHELL_PATH, argv);
    } else {
        execvp(argv[0], argv);
    }

    /* execvp failed — try via shell */
    {
        const char *shell_argv[] = { SHELL_PATH, "-c", cmd, NULL };
        execvp(SHELL_PATH, (char *const *)shell_argv);
    }

    /* Both execs failed */
    (void)write(STDERR_FILENO, "tinit: exec failed: ", 20);
    (void)write(STDERR_FILENO, cmd, strlen(cmd));
    (void)write(STDERR_FILENO, "\n", 1);
    _exit(127);
}

/* ------------------------------------------------------------------ */

pid_t spawn_entry(inittab_entry_t *e)
{
    pid_t pid;
    int   askfirst = (e->action == A_ASKFIRST);

    /* Block signals across fork so the child gets a clean mask */
    sig_block_all();

    pid = fork();
    if (pid < 0) {
        log_error("fork: %s", strerror(errno));
        sig_unblock_all();
        return -1;
    }

    if (pid == 0) {
        /* ---- child ---- */
        /* Close the signal pipe — child must not interfere */
        close(sig_pipe_r);
        /* child_exec never returns */
        child_exec(e->process, askfirst);
        /* unreachable */
        _exit(1);
    }

    /* ---- parent ---- */
    sig_unblock_all();
    e->pid    = pid;
    e->exited = 0;
    log_info("spawned '%s' pid=%d (action=%d)", e->process, pid, (int)e->action);
    return pid;
}

/* ------------------------------------------------------------------ */
/* Non-blocking zombie reap: mark matching entries as exited           */
/* ------------------------------------------------------------------ */
void reap_children(void)
{
    pid_t  pid;
    int    status;
    int    i;

    for (;;) {
        pid = waitpid(-1, &status, WNOHANG);
        if (pid <= 0)
            break;

        /* Find the entry that owned this pid */
        for (i = 0; i < g_state.num_entries; i++) {
            if (g_state.entries[i].pid == pid) {
                inittab_entry_t *e = &g_state.entries[i];
                e->pid    = 0;
                e->exited = 1;
                if (WIFEXITED(status))
                    log_info("pid=%d '%s' exited status=%d",
                             pid, e->process, WEXITSTATUS(status));
                else if (WIFSIGNALED(status))
                    log_info("pid=%d '%s' killed by signal %d",
                             pid, e->process, WTERMSIG(status));
                break;
            }
        }

        /* A process we didn't track (from /etc/inittab sysinit, etc.) */
        if (i == g_state.num_entries) {
            if (WIFEXITED(status))
                log_info("reaped unknown pid=%d exit=%d", pid, WEXITSTATUS(status));
        }
    }
}

/* ------------------------------------------------------------------ */
/* Run a command and wait for it to finish (for sysinit/wait entries)  */
/* ------------------------------------------------------------------ */
int run_and_wait(const char *cmd)
{
    pid_t pid;
    int   status = 0;

    sig_block_all();
    pid = fork();

    if (pid < 0) {
        log_error("fork: %s", strerror(errno));
        sig_unblock_all();
        return -1;
    }

    if (pid == 0) {
        /* child */
        close(sig_pipe_r);
        child_exec(cmd, 0);
        /* unreachable */
        _exit(1);
    }

    sig_unblock_all();
    log_info("running '%s' (wait) pid=%d", cmd, pid);

    /* Wait specifically for this child, reaping others along the way */
    for (;;) {
        pid_t w = waitpid(pid, &status, 0);
        if (w == pid)
            break;
        if (w < 0) {
            if (errno == EINTR) continue;
            break;
        }
        /* Reap any other zombie that arrived first */
        reap_children();
    }

    if (WIFEXITED(status))
        return WEXITSTATUS(status);
    return -1;
}
