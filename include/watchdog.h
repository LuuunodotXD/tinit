#ifndef WATCHDOG_H
#define WATCHDOG_H

/*
 * watchdog.h — hardware watchdog keepalive for tinit
 *
 * PID 1 is the natural owner of the watchdog: if the init process
 * freezes, the whole system is considered hung and the hardware timer
 * fires a reset.
 *
 * Interface (Linux):
 *   open("/dev/watchdog")       — starts the timer
 *   write(fd, anything, 1)      — resets ("kicks") the timer
 *   write(fd, "V", 1) + close   — magic close: disables the watchdog
 *                                  (only if driver supports WDIOF_MAGICCLOSE)
 *
 * We kick every WATCHDOG_KICK_INTERVAL seconds.
 * We try to set the hardware timeout to WATCHDOG_TIMEOUT via ioctl;
 * if the ioctl is not available, we rely on the driver default.
 *
 * Call watchdog_tick() once per second from the main loop.
 * Call watchdog_close() during shutdown before reboot(2).
 */

#define WATCHDOG_DEV           "/dev/watchdog"
#define WATCHDOG_TIMEOUT       60    /* seconds — requested HW timeout     */
#define WATCHDOG_KICK_INTERVAL 15    /* seconds — how often we kick it     */

/* fd of the open watchdog device (-1 if not open / not available) */
extern int watchdog_fd;

/*
 * Open /dev/watchdog and configure timeout.
 * Non-fatal: if the device doesn't exist, logs a warning and returns -1.
 * Once opened, the watchdog WILL fire unless we keep kicking or close
 * with magic 'V'.  Always call watchdog_close() on shutdown.
 */
int watchdog_open(void);

/*
 * Call once per second from the main loop.
 * Kicks the watchdog every WATCHDOG_KICK_INTERVAL seconds.
 */
void watchdog_tick(void);

/*
 * Disable watchdog and close fd.
 * Writes magic 'V' first so drivers that support WDIOF_MAGICCLOSE
 * won't fire after we close.
 * Call during shutdown BEFORE reboot(2).
 */
void watchdog_close(void);

#endif /* WATCHDOG_H */
