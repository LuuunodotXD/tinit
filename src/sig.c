/*
 * sig.c — signal handling for tinit (PID 1)
 *
 * We use the classic self-pipe trick so the main loop can use select()
 * without needing Linux-specific signalfd(2).  This works identically
 * on glibc, musl, and uclibc.
 *
 * Contract with main.c:
 *   - Call sig_init() once after log_init().
 *   - Monitor sig_pipe_r with select()/poll() in the main loop.
 *   - Read one byte from sig_pipe_r; the byte IS the signal number.
 */


#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>

#include "tinit.h"
#include "log.h"
#include "sig.h"

int sig_pipe_r = -1;   /* main loop reads from here */
static int sig_pipe_w = -1;

/* ------------------------------------------------------------------ */
/* Async-signal-safe handler: write one byte (the signal number)       */
/* ------------------------------------------------------------------ */
static void pipe_handler(int signo)
{
    unsigned char c = (unsigned char)signo;
    ssize_t n;
    /* write() is async-signal-safe per POSIX */
    do { n = write(sig_pipe_w, &c, 1); } while (n < 0 && errno == EINTR);
    (void)n; /* ignore errors — pipe full means signal dropped, but that's OK */
}

/* ------------------------------------------------------------------ */

static void install(int signo, void (*handler)(int))
{
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handler;
    sigemptyset(&sa.sa_mask);
    /* Do NOT set SA_RESTART: we want EINTR in blocking calls */
    if (sigaction(signo, &sa, NULL) < 0)
        log_warn("sigaction(%d): %s", signo, strerror(errno));
}

/* ------------------------------------------------------------------ */

void sig_init(void)
{
    int fds[2];

    if (pipe(fds) < 0) {
        log_error("pipe: %s — signals will not work!", strerror(errno));
        return;
    }

    sig_pipe_r = fds[0];
    sig_pipe_w = fds[1];

    /* Set write end non-blocking so we never block in the handler */
    {
        int fl = fcntl(sig_pipe_w, F_GETFL);
        if (fl >= 0) fcntl(sig_pipe_w, F_SETFL, fl | O_NONBLOCK);
    }
    /* Set close-on-exec on both ends */
    fcntl(sig_pipe_r, F_SETFD, FD_CLOEXEC);
    fcntl(sig_pipe_w, F_SETFD, FD_CLOEXEC);

    /*
     * PID 1 signal assignments:
     *
     *  SIGCHLD  — child exited (zombie reaping)
     *  SIGINT   — Ctrl+Alt+Del (kernel sends to PID 1)
     *  SIGTERM  — orderly halt
     *  SIGUSR1  — halt (alternative, e.g. from shutdown(8))
     *  SIGUSR2  — power off
     *  SIGHUP   — reload inittab
     *
     * We ignore SIGPIPE globally (write to closed pipe returns EPIPE).
     */
    install(SIGCHLD,  pipe_handler);
    install(SIGINT,   pipe_handler);
    install(SIGTERM,  pipe_handler);
    install(SIGUSR1,  pipe_handler);
    install(SIGUSR2,  pipe_handler);
    install(SIGHUP,   pipe_handler);
    install(SIGPIPE,  SIG_IGN);
    install(SIGTTOU,  SIG_IGN);   /* background writes to tty */
    install(SIGTTIN,  SIG_IGN);   /* background reads from tty */

    log_info("signal handlers installed (pipe_r=%d)", sig_pipe_r);
}

/* ------------------------------------------------------------------ */
/* Block ALL catchable signals — call before fork() so children do    */
/* NOT inherit our pipe.  Children call sig_unblock_all() after fork. */
/* ------------------------------------------------------------------ */
void sig_block_all(void)
{
    sigset_t full;
    sigfillset(&full);
    sigprocmask(SIG_SETMASK, &full, NULL);
}

void sig_unblock_all(void)
{
    sigset_t empty;
    sigemptyset(&empty);
    sigprocmask(SIG_SETMASK, &empty, NULL);

    /* Restore defaults that PID 1 had overridden */
    signal(SIGTERM,  SIG_DFL);
    signal(SIGINT,   SIG_DFL);
    signal(SIGUSR1,  SIG_DFL);
    signal(SIGUSR2,  SIG_DFL);
    signal(SIGHUP,   SIG_DFL);
    signal(SIGCHLD,  SIG_DFL);
    signal(SIGPIPE,  SIG_DFL);
    signal(SIGTTOU,  SIG_DFL);
    signal(SIGTTIN,  SIG_DFL);
}
