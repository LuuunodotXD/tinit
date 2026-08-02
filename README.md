# tinit

**Tiny, portable Linux init system — busybox-init compatible.**

Single binary, C99, zero runtime dependencies beyond libc.  
Designed to be PID 1 in embedded systems, containers, and initramfs images.

Current version: **0.3.0**

---

## Features

| Feature | Notes |
|---|---|
| `/etc/inittab` | busybox-compatible format |
| Zombie reaping | all orphaned children, not just direct spawns |
| Signal-safe | self-pipe trick, no `signalfd` |
| Early mounts | proc / sysfs / devtmpfs / devpts / run / tmp |
| Graceful shutdown | SIGTERM→SIGKILL→sync→umount→reboot(2) |
| Ctrl+Alt+Del | kernel SIGINT → `ctrlaltdel` action |
| `inittab` reload | SIGHUP |
| No stdio in critical paths | `write()` only on `/dev/console` |
| `/proc/cmdline` parser | `quiet`, `debug`, `console=`, `init=`, `tinit.*` |
| `/dev/initctl` FIFO | sysvinit wire protocol — `shutdown(8)`, `reboot(8)` compatible |
| `switch_root` | initramfs → real rootfs handoff (`pivot_root` + `MS_MOVE` fallback) |
| Hardware watchdog | `/dev/watchdog` keepalive, magic close on shutdown |
| `sysctl` loader | `/etc/sysctl.conf` + `/etc/sysctl.d/` in standard load order |
| Environment loader | `/etc/environment` + `/etc/environment.d/` with sane defaults |
| dietlibc support | `compat.h` shim layer; `diet` wrapper integration in Makefile |

---

## Architectures & libc

| Architecture | glibc | musl | uclibc | dietlibc |
|---|:---:|:---:|:---:|:---:|
| x86 (i686)    | ✓ | ✓ | ✓ | ✓ |
| x86\_64       | ✓ | ✓ | ✓ | ✓ |
| aarch64       | ✓ | ✓ | ✓ | ✓ |
| armhf (v7hf)  | ✓ | ✓ | ✓ | ✓ |
| armv7 (eabi)  | ✓ | ✓ | ✓ | ✓ |
| loongarch64   | ✓ | ✓ | — | — |
| ppc64le       | ✓ | ✓ | — | ✓ |
| riscv64       | ✓ | ✓ | — | — |
| s390x         | ✓ | ✓ | — | ✓ |

dietlibc arch support follows upstream dietlibc (Felix von Leitner);
loongarch64 and riscv64 are not yet upstream.

---

## Source layout

```
tinit/
├── include/
│   ├── tinit.h       common types, macros, NORETURN, MAX_*, version
│   ├── compat.h      portability shims — injected via -include flag
│   ├── log.h         console logging (write-based, signal-safe raw path)
│   ├── mount.h       early-boot mount / umount_all
│   ├── sig.h         signal setup, self-pipe, block/unblock helpers
│   ├── process.h     spawn_entry, reap_children, run_and_wait
│   ├── inittab.h     parser, default table, action enum
│   ├── cmdline.h     /proc/cmdline parser, g_cmdline struct
│   ├── initctl.h     /dev/initctl FIFO, sysvinit wire protocol
│   ├── switchroot.h  pivot_root / MS_MOVE root handoff
│   ├── watchdog.h    /dev/watchdog keepalive
│   ├── sysctl.h      sysctl.conf + sysctl.d/ loader
│   └── env.h         /etc/environment loader
├── src/
│   ├── main.c        PID 1 entry point, full boot sequence, main loop
│   ├── log.c         vsnprintf → write() to /dev/console
│   ├── mount.c       mount(2) wrappers, devtmpfs fallback + mknod
│   ├── sig.c         sigaction, self-pipe, sig_block/unblock
│   ├── process.c     fork/exec, waitpid, zombie reap, run_and_wait
│   ├── inittab.c     fgets-based parser, dump, built-in default table
│   ├── cmdline.c     reads /proc/cmdline, populates g_cmdline
│   ├── initctl.c     creates FIFO, O_RDWR trick, request dispatch
│   ├── switchroot.c  pivot_root(2) via syscall(), MS_MOVE fallback
│   ├── watchdog.c    open/kick/magic-close, WDIOC_* with #ifdef guard
│   ├── sysctl.c      key→/proc/sys/ translation, dir scan with qsort
│   └── env.c         setenv(3), quote stripping, valid_key() guard
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
make LIBC=dietlibc      # use diet wrapper (must be in PATH)
```

### Cross-compile

```sh
make ARCH=aarch64  LIBC=musl
make ARCH=armhf    LIBC=glibc
make ARCH=riscv64  LIBC=musl
make ARCH=ppc64le  LIBC=glibc
make ARCH=s390x    LIBC=dietlibc
```

The Makefile resolves the cross-compiler prefix automatically.
If your toolchain uses a different prefix, pass `CC` directly:

```sh
make ARCH=aarch64 LIBC=musl CC=aarch64-unknown-linux-musl-gcc
```

For dietlibc cross-compilation the Makefile prepends `diet` to the
resolved glibc cross-compiler prefix:

```sh
# Results in: diet aarch64-linux-gnu-gcc
make ARCH=aarch64 LIBC=dietlibc
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

### switch\_root as a standalone binary

```sh
ln -s /sbin/init /sbin/switch_root
switch_root /newroot /sbin/init
```

---

## Portability notes

### compat.h — the shim layer

`compat.h` is injected into every translation unit via the Makefile
flag `-include $(INCDIR)/compat.h`.  It provides:

| Shim | Condition | Reason |
|---|---|---|
| `tinit_basename()` / `TINIT_BASENAME()` | always | eliminates `libgen.h` dependency |
| `tinit_strtok_r()` | always | private tokenizer, never depends on libc version |
| `umount2()` via `syscall()` | `__dietlibc__` | dietlibc has `umount()` but not `umount2()` |
| `MNT_DETACH 2` | `__dietlibc__` | absent from dietlibc's `<sys/mount.h>` |
| `SYS_umount2` per-arch fallback | `__dietlibc__` | if `<sys/syscall.h>` doesn't define it |
| `<sys/sysmacros.h>` | `!__dietlibc__` | `makedev()` on glibc ≥ 2.27 is an inline function |
| `<strings.h>` | `!__dietlibc__` | `strcasecmp` is in `<string.h>` on dietlibc |

**Non-obvious pitfall:** glibc's `<sys/mount.h>` defines `MNT_DETACH`
both as an enum member *and* as `#define MNT_DETACH MNT_DETACH`
(self-referential).  Any `#define MNT_DETACH 2` placed before
`<sys/mount.h>` is processed causes a cryptic "expected identifier
before numeric constant" error at the `#define` line itself — not at
the point of conflict.  The rule adopted: **nothing that could conflict
with a system header is defined outside `#ifdef __dietlibc__`**.

### No glibc extensions used

| Avoided | Replaced with |
|---|---|
| `TEMP_FAILURE_RETRY` | explicit `do { } while (errno == EINTR)` |
| `execvpe()` | `execvp()` (env inherited naturally) |
| `getline()` | `fgets()` — C89, available everywhere |
| `signalfd()` | self-pipe + `select()` |
| `clearenv()` | not needed |
| `glob()` | `opendir` / `readdir` / `qsort` |
| `pivot_root()` libc wrapper | `syscall(SYS_pivot_root, ...)` directly |
| `basename()` from `libgen.h` | `TINIT_BASENAME()` from `compat.h` |
| `strtok_r()` from libc | `tinit_strtok_r()` from `compat.h` |

### dietlibc-specific notes

- `__dietlibc__` is defined automatically by the `diet` wrapper.
- `diet` handles sysroot and static linking; `LDFLAGS += -static` is
  suppressed in the Makefile to avoid a warning from the wrapper.
- For cross-compilation: `diet {cross-prefix}gcc`
  (e.g. `diet arm-linux-gnueabihf-gcc`).
- dietlibc's `<sys/mount.h>` may omit `MNT_DETACH` — provided by
  `compat.h`.
- `umount2(2)` is not in dietlibc's public API — provided via
  `syscall(SYS_umount2)` in `compat.h` with per-arch `SYS_umount2`
  fallbacks for x86, x86\_64, aarch64, arm, ppc, s390.
- `strcasecmp` lives in `<string.h>` on dietlibc (not `<strings.h>`).
- `makedev()` is in `<sys/stat.h>` on dietlibc.

### C standard

`-std=c99 -D_GNU_SOURCE`.  No VLAs, no C11 atomics, no `__extension__`
tricks.  `_GNU_SOURCE` is honoured identically by glibc, musl, uclibc,
and dietlibc on Linux.

---

## /proc/cmdline parameters

| Parameter | Effect |
|---|---|
| `quiet` | Raise kernel printk threshold; suppress tinit info logs |
| `debug` | Alias for `tinit.loglevel=0`; also dumps cmdline + inittab |
| `console=ttyS0,115200` | Stored in `g_cmdline.console` (informational) |
| `init=/sbin/init` | Stored in `g_cmdline.init` (used by switch\_root) |
| `tinit.loglevel=N` | 0=debug 1=info 2=warn 3=error 4=quiet |
| `tinit.debug` | Same as `debug` above |

---

## /dev/initctl protocol

tinit creates `/dev/initctl` as a FIFO (mode 0600) and monitors it via
`select()` alongside the signal pipe.  Wire format: sysvinit
`struct init_request` (magic `0x03091969`).

| Tool | Request | tinit action |
|---|---|---|
| `shutdown -h now` | `RUNLVL '0'` | halt |
| `shutdown -r now` | `RUNLVL '6'` | reboot |
| `reboot` | `RUNLVL '6'` | reboot |
| `poweroff` | `RUNLVL '0'` | halt / power off |
| `shutdown -c` | `POWEROK` | cancel pending shutdown |
| UPS daemon | `POWERFAIL` | power-off after grace period |
| UPS daemon | `POWERFAILNOW` | immediate power-off |

**Implementation detail:** FIFO opened `O_RDWR` so tinit holds both
ends — prevents `select()` from returning EOF and busy-looping when all
userspace writers close their end.

---

## switch\_root

```
initramfs (tmpfs)
│
├─ tinit as PID 1
├─ [sysinit] mount /dev/sda1 on /newroot
└─ [wait]    switch_root /newroot /sbin/init
                │
                ├─ move /proc /sys /dev /run /tmp into /newroot
                ├─ try pivot_root(2)           [preferred]
                │   └─ fallback: MS_MOVE + chroot
                └─ execv(/sbin/init)           [never returns]
```

`pivot_root(2)` called via `syscall(SYS_pivot_root)` — no libc wrapper
needed.

---

## Hardware watchdog

```
boot
 └─ watchdog_open()     starts the HW timer immediately
      └─ WDIOC_SETTIMEOUT  try to set timeout to 60 s
main loop (every second)
 └─ watchdog_tick()     kicks via WDIOC_KEEPALIVE or write()
shutdown
 └─ watchdog_close()    write('V') — magic close disables the timer
      └─ called BEFORE kill(-1, SIGTERM) so a stuck shutdown
         triggers a hardware reset rather than hanging forever
```

`<linux/watchdog.h>` is conditionally included via `__has_include`.
Falls back to `write(fd, "1", 1)` when absent.

---

## sysctl loader

Load order (later = higher priority):

```
/usr/lib/sysctl.d/ *.conf   vendor / package defaults
/run/sysctl.d/ *.conf       runtime overrides
/etc/sysctl.d/ *.conf       admin drop-ins
/etc/sysctl.conf            admin legacy (highest priority)
```

Files within each directory applied in lexicographic order.
Directory scanning uses `opendir` / `readdir` / `qsort` — no `glob()`.

Key format: `net.ipv4.ip_forward = 1` → `/proc/sys/net/ipv4/ip_forward`

Leading `-` on a key silences errors: `-net.ipv6.conf.all.disable_ipv6 = 0`

---

## Environment loader

```sh
# /etc/environment — no shell expansion, no substitution
PATH=/usr/local/bin:/usr/bin:/bin
LANG=en_US.UTF-8
TZ=UTC
```

Supported syntax: `KEY=value`, `KEY="value with spaces"`,
`KEY='value'`, `export KEY=value`.

Defaults applied with `overwrite=0` (never clobber file-defined values):

| Variable | Default |
|---|---|
| `PATH` | `/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin` |
| `TERM` | `linux` |
| `HOME` | `/root` |
| `SHELL` | `/bin/sh` |
| `LANG` | `C.UTF-8` |

---

## inittab format

```
id:runlevels:action:process
```

`runlevels` is **ignored** — tinit has no runlevel concept.

| Action | Description |
|---|---|
| `sysinit` | Phase 1 — run serially, wait. Use for fsck, hostname, sysctl. |
| `boot` | Phase 2 — run, no wait. |
| `bootwait` | Phase 2 — run, wait. |
| `wait` | Run and wait (alias for `bootwait`). |
| `once` | Run once, no wait, no restart. |
| `respawn` | Restart whenever process exits. |
| `askfirst` | Like `respawn`, prints "Press Enter..." first. |
| `ctrlaltdel` | Run on kernel SIGINT (Ctrl+Alt+Del). |
| `shutdown` | Run during shutdown before unmounting. |
| `restart` | Run to re-exec init. |

---

## Signal reference (PID 1)

| Signal | Source | tinit action |
|---|---|---|
| `SIGCHLD` | kernel | reap zombie children |
| `SIGINT` | kernel (Ctrl+Alt+Del) | run `ctrlaltdel` entry → reboot |
| `SIGTERM` | userspace | halt |
| `SIGUSR1` | userspace | halt (alternative) |
| `SIGUSR2` | userspace | power off |
| `SIGHUP` | userspace | reload `/etc/inittab` |

---

## Boot sequence

```
PID 1 starts
│
├─ log_init()            open /dev/console
├─ cmdline_parse()       read /proc/cmdline
├─ sig_init()            install handlers, create self-pipe
├─ mount_rootfs_rw()     remount / read-write
├─ mount_early()         proc, sysfs, devtmpfs, devpts, /run, /tmp
├─ env_load_all()        /etc/environment.d/ + /etc/environment
├─ sysctl_load_all()     /usr/lib/sysctl.d/ → /etc/sysctl.conf
├─ watchdog_open()       start HW timer (non-fatal if absent)
├─ initctl_open()        create /dev/initctl FIFO
├─ inittab_parse()       read /etc/inittab (or built-in default)
│
├─ [sysinit] entries     serial, wait each
├─ [boot/bootwait]       once, optionally wait
│
└─ main loop  ◄──────────────────────────────────────────────────┐
   │                                                              │
   ├─ start_respawn()    fork getty/shell if not running          │
   ├─ watchdog_tick()    kick /dev/watchdog every 15 s            │
   ├─ select(sig_pipe, initctl_fd, timeout=1s)                    │
   │   ├─ SIGCHLD    → reap_children() ────────────────────────►─┘
   │   ├─ SIGINT     → ctrlaltdel entry → reboot
   │   ├─ SIGTERM    → reboot_cmd = HALT,    running = 0
   │   ├─ SIGUSR1    → reboot_cmd = HALT,    running = 0
   │   ├─ SIGUSR2    → reboot_cmd = PWROFF,  running = 0
   │   ├─ SIGHUP     → inittab_parse() reload
   │   └─ initctl_fd → dispatch RUNLVL/POWERFAIL/etc.
   │
   └─ running == 0
       ├─ run [shutdown] entries
       ├─ initctl_close()
       ├─ watchdog_close()    write 'V', disable HW timer
       ├─ kill(-1, SIGTERM)
       ├─ sleep(1)
       ├─ kill(-1, SIGKILL)
       ├─ umount_all() + sync()
       └─ reboot(2)
```

---

## Changelog

### 0.3.0
- dietlibc support: `compat.h` shim layer, `diet` wrapper in Makefile
- `TINIT_BASENAME()` / `tinit_strtok_r()`: internal replacements for
  `basename()` and `strtok_r()`, removing `libgen.h` dependency
- Fixed: `MNT_DETACH` and `makedev` conflicts with glibc's
  self-referential macros and inline functions when using `-include`

### 0.2.0
- `/dev/initctl` FIFO (sysvinit wire protocol)
- `switch_root`: `pivot_root(2)` + `MS_MOVE` fallback
- `/proc/cmdline` parser with `tinit.*` namespace
- Hardware watchdog (`/dev/watchdog`), magic close on shutdown
- `sysctl` loader: `/etc/sysctl.conf` + `/etc/sysctl.d/`
- `/etc/environment` loader with quote stripping and key validation

### 0.1.0
- Initial release: inittab, zombie reaping, self-pipe signals,
  early mounts, graceful shutdown, Ctrl+Alt+Del, `inittab` reload

---

## License

The Unlicense — do whatever you want with it.
