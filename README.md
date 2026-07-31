# tinit

**Tiny, portable Linux init system — busybox-init compatible.**

Single binary, C99, zero runtime dependencies beyond libc.
Designed to be PID 1 in embedded systems, containers, and initramfs images.

---

## Features

| Feature | Notes |
|---|---|
| `/etc/inittab` | busybox-compatible format |
| Zombie reaping | all orphaned children, not just direct |
| Signal-safe | self-pipe trick, no `signalfd` |
| Early mounts | proc / sysfs / devtmpfs / devpts / run / tmp |
| Graceful shutdown | SIGTERM→SIGKILL→sync→umount→reboot(2) |
| Ctrl+Alt+Del | kernel SIGINT → `ctrlaltdel` action |
| `inittab` reload | SIGHUP |
| No stdio in critical paths | `write()` only on `/dev/console` |

---

## Architectures & libc

| Architecture | glibc | musl | uclibc |
|---|:---:|:---:|:---:|
| x86 (i686)    | ✓ | ✓ | ✓ |
| x86\_64       | ✓ | ✓ | ✓ |
| aarch64       | ✓ | ✓ | ✓ |
| armhf (v7hf)  | ✓ | ✓ | ✓ |
| armv7 (eabi)  | ✓ | ✓ | ✓ |
| loongarch64   | ✓ | ✓ | — |
| ppc64le       | ✓ | ✓ | — |
| riscv64       | ✓ | ✓ | — |
| s390x         | ✓ | ✓ | — |

---

## Source layout

```
tinit/
├── include/
│   ├── tinit.h       common types, macros, NORETURN, MAX_*
│   ├── log.h         console logging (write-based, signal-safe raw path)
│   ├── mount.h       early-boot mount / umount_all
│   ├── sig.h         signal setup, self-pipe, block/unblock helpers
│   ├── process.h     spawn_entry, reap_children, run_and_wait
│   └── inittab.h     parser, default table, action enum
├── src/
│   ├── main.c        PID 1 entry point, boot sequence, main loop
│   ├── log.c         vsnprintf → write() to /dev/console
│   ├── mount.c       mount(2) wrappers, devtmpfs fallback
│   ├── sig.c         sigaction, self-pipe, sig_block/unblock
│   ├── process.c     fork/exec, waitpid, zombie reap, run_and_wait
│   └── inittab.c     fgets-based parser, dump, default table
├── Makefile          multi-arch / multi-libc, matrix target
├── inittab.example   annotated sample /etc/inittab
└── README.md         this file
```

---

## Build

### Native (host architecture)

```sh
make                    # uses $(uname -m), musl by default
make LIBC=glibc         # use host glibc
```

### Cross-compile

```sh
make ARCH=aarch64  LIBC=musl
make ARCH=armhf    LIBC=glibc
make ARCH=riscv64  LIBC=musl
make ARCH=ppc64le  LIBC=glibc
make ARCH=s390x    LIBC=musl
```

The Makefile resolves the cross-compiler prefix automatically from the
table in `Makefile`.  If your toolchain uses a different prefix, pass
`CC` directly:

```sh
make ARCH=aarch64 LIBC=musl CC=aarch64-unknown-linux-musl-gcc
```

### Build all targets (matrix)

```sh
make matrix
```

Targets whose toolchain is not installed are skipped with a warning.

### Debug build

```sh
make DEBUG=1 ARCH=x86_64 LIBC=glibc
```

### Strip and install

```sh
make strip install DESTDIR=/path/to/rootfs
# installs as /path/to/rootfs/sbin/init
```

---

## Portability notes

### No glibc extensions used

| Avoided | Replaced with |
|---|---|
| `TEMP_FAILURE_RETRY` | explicit `do { } while (errno == EINTR)` |
| `execvpe()` | `execvp()` (env inherited) |
| `getline()` | `fgets()` |
| `signalfd()` | self-pipe + `select()` |
| `clearenv()` | not needed |

### uclibc caveats

- Some uclibc builds omit `RB_POWER_OFF`; we guard with `#ifdef`.
- `ioctl(TIOCSCTTY)` availability depends on kernel config.
- uclibc cross-compiler naming varies by buildroot config; set `CC`
  explicitly when using `uclibc`.

### C standard

`-std=c99` with `-D_POSIX_C_SOURCE=200809L`.  No VLAs, no C11 features,
no `__extension__` tricks.

---

## inittab format

```
id:runlevels:action:process
```

`runlevels` is **ignored** (tinit has no runlevel concept).

### Supported actions

| Action | Description |
|---|---|
| `sysinit` | First phase — run serially, wait. Use for fsck, hostname, etc. |
| `boot` | Second phase — run, no wait. |
| `bootwait` | Second phase — run, wait. |
| `wait` | Run and wait (alias for `bootwait`). |
| `once` | Run once, no wait, no restart. |
| `respawn` | Restart whenever process exits. Use for getty, services. |
| `askfirst` | Like `respawn`, prints "Press Enter..." first. |
| `ctrlaltdel` | Run on kernel SIGINT (Ctrl+Alt+Del keypress). |
| `shutdown` | Run during shutdown sequence. |
| `restart` | Run to re-exec init. |

---

## Signal reference (PID 1)

| Signal | Meaning |
|---|---|
| `SIGCHLD` | Child exited — reap zombies |
| `SIGINT` | Ctrl+Alt+Del (sent by kernel to PID 1) → reboot |
| `SIGTERM` | Halt system |
| `SIGUSR1` | Halt system (alternative) |
| `SIGUSR2` | Power off |
| `SIGHUP` | Reload `/etc/inittab` |

---

## Boot sequence

```
PID 1 starts
│
├─ log_init()          open /dev/console
├─ sig_init()          install handlers, create self-pipe
├─ mount_rootfs_rw()   remount / read-write
├─ mount_early()       proc, sysfs, devtmpfs, devpts, /run, /tmp
├─ inittab_parse()     read /etc/inittab (or built-in default)
│
├─ [sysinit] entries   serial, wait each
├─ [boot/bootwait]     once, optionally wait
│
└─ main loop  ◄────────────────────────────────────────┐
   │                                                    │
   ├─ start_respawn()  fork getty/shell if not running  │
   ├─ select(sig_pipe) wait for signal                  │
   │   ├─ SIGCHLD → reap_children() ──────────────────►┘
   │   ├─ SIGINT  → reboot_cmd = REBOOT, running = 0
   │   ├─ SIGTERM → reboot_cmd = HALT,   running = 0
   │   ├─ SIGUSR2 → reboot_cmd = PWROFF, running = 0
   │   └─ SIGHUP  → inittab_parse() reload
   │
   └─ running == 0
       ├─ run shutdown entries
       ├─ kill -TERM all
       ├─ kill -KILL all
       ├─ umount_all() + sync()
       └─ reboot(2)
```

---

## License

The Unlicense — do whatever you want with it.
