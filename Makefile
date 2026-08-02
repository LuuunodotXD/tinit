# =============================================================================
# tinit — Makefile
#
# Usage:
#   make                         # native build (auto-detects arch)
#   make ARCH=aarch64 LIBC=musl  # cross-build
#   make matrix                  # build all supported targets
#   make clean
#
# Supported ARCH values:
#   x86  x86_64  aarch64  armhf  armv7
#   loongarch64  ppc64le  riscv64  s390x
#
# Supported LIBC values:
#   glibc  musl  uclibc
# =============================================================================

# --- Defaults ----------------------------------------------------------------
ARCH    ?= $(shell uname -m | sed 's/i.86/x86/')
LIBC    ?= musl
TARGET  := $(ARCH)-$(LIBC)

# --- Source & object layout --------------------------------------------------
SRCDIR  := src
INCDIR  := include
OUTDIR  := out/$(TARGET)

SRCS    := $(wildcard $(SRCDIR)/*.c)
OBJS    := $(patsubst $(SRCDIR)/%.c,$(OUTDIR)/%.o,$(SRCS))
BIN     := $(OUTDIR)/tinit

# --- Cross-compiler table ----------------------------------------------------
#
# Format: CROSS_PREFIX_<ARCH>_<LIBC>
#
# Add your toolchain prefix here.  Buildroot / crosstool-NG and Debian
# multiarch packages use slightly different naming conventions; the table
# below covers the most common ones.  uclibc prefixes vary by toolchain —
# override CC directly when needed.
#
CROSS_PREFIX_x86_glibc        := i686-linux-gnu-
CROSS_PREFIX_x86_musl         := i686-linux-musl-
CROSS_PREFIX_x86_uclibc       := i686-linux-uclibc-
CROSS_PREFIX_x86_dietlibc     := i686-linux-gnu-   # diet wrapper prepended below

CROSS_PREFIX_x86_64_glibc     := x86_64-linux-gnu-
CROSS_PREFIX_x86_64_musl      := x86_64-linux-musl-
CROSS_PREFIX_x86_64_uclibc    := x86_64-linux-uclibc-
CROSS_PREFIX_x86_64_dietlibc  := x86_64-linux-gnu-

CROSS_PREFIX_aarch64_glibc    := aarch64-linux-gnu-
CROSS_PREFIX_aarch64_musl     := aarch64-linux-musl-
CROSS_PREFIX_aarch64_uclibc   := aarch64-linux-uclibc-
CROSS_PREFIX_aarch64_dietlibc := aarch64-linux-gnu-

# armhf = ARMv7 hard-float ABI (Raspberry Pi OS, Debian armhf)
CROSS_PREFIX_armhf_glibc      := arm-linux-gnueabihf-
CROSS_PREFIX_armhf_musl       := arm-linux-musleabihf-
CROSS_PREFIX_armhf_uclibc     := arm-buildroot-linux-uclibcgnueabihf-
CROSS_PREFIX_armhf_dietlibc   := arm-linux-gnueabihf-

# armv7 = ARMv7 soft-float ABI
CROSS_PREFIX_armv7_glibc      := arm-linux-gnueabi-
CROSS_PREFIX_armv7_musl       := arm-linux-musleabi-
CROSS_PREFIX_armv7_uclibc     := arm-linux-uclibceabi-
CROSS_PREFIX_armv7_dietlibc   := arm-linux-gnueabi-

CROSS_PREFIX_loongarch64_glibc  := loongarch64-linux-gnu-
CROSS_PREFIX_loongarch64_musl   := loongarch64-linux-musl-
CROSS_PREFIX_loongarch64_uclibc := loongarch64-linux-uclibc-

CROSS_PREFIX_ppc64le_glibc    := powerpc64le-linux-gnu-
CROSS_PREFIX_ppc64le_musl     := powerpc64le-linux-musl-
CROSS_PREFIX_ppc64le_uclibc   := powerpc64le-linux-uclibc-
CROSS_PREFIX_ppc64le_dietlibc := powerpc64le-linux-gnu-

CROSS_PREFIX_riscv64_glibc    := riscv64-linux-gnu-
CROSS_PREFIX_riscv64_musl     := riscv64-linux-musl-
CROSS_PREFIX_riscv64_uclibc   := riscv64-linux-uclibc-

CROSS_PREFIX_s390x_glibc      := s390x-linux-gnu-
CROSS_PREFIX_s390x_musl       := s390x-linux-musl-
CROSS_PREFIX_s390x_uclibc     := s390x-linux-uclibc-
CROSS_PREFIX_s390x_dietlibc   := s390x-linux-gnu-

# Resolve prefix for the requested target
_KEY            := $(subst -,_,$(TARGET))
CROSS_PREFIX    ?= $(CROSS_PREFIX_$(_KEY))

# If no prefix resolved AND we're cross-compiling, emit a warning
ifeq ($(CROSS_PREFIX),)
  ifneq ($(ARCH),$(shell uname -m | sed 's/i.86/x86/'))
    $(warning No cross-compiler prefix found for $(TARGET). Set CC explicitly.)
  endif
endif

# --- Compiler / flags --------------------------------------------------------
CC      ?= $(CROSS_PREFIX)gcc
AR      ?= $(CROSS_PREFIX)ar
STRIP   ?= $(CROSS_PREFIX)strip
# 'size' reads any ELF regardless of arch — use the native one.
# Override with SIZE=$(CROSS_PREFIX)size if you have the full cross-binutils.
SIZE    ?= size

# dietlibc: wrap CC with the 'diet' frontend.
# diet handles sysroot + linking; the underlying CC is the glibc cross-gcc.
# For cross-compilation: make ARCH=aarch64 LIBC=dietlibc
#   → CC becomes: diet aarch64-linux-gnu-gcc
ifeq ($(LIBC),dietlibc)
  override CC := diet $(CC)
  # diet wrapper already forces -static; remove it from LDFLAGS to avoid
  # "diet: -static not supported" warnings on some versions.
  LDFLAGS := $(filter-out -static,$(LDFLAGS))
endif

# Base flags (C99, strict POSIX, no implicit function declarations)
# -include compat.h: inject our portability shims into every translation
# unit without requiring each .c file to explicitly include it.
CFLAGS  ?= -std=c99 \
            -Wall -Wextra -Wshadow -Wstrict-prototypes \
            -Wmissing-prototypes -Wno-unused-parameter \
            -Wno-unused-result \
            -D_GNU_SOURCE \
            -include $(INCDIR)/compat.h \
            -I$(INCDIR)

# Release flags (override with DEBUG=1)
ifeq ($(DEBUG),1)
  CFLAGS += -g3 -O0 -DDEBUG
else
  CFLAGS += -Os -ffunction-sections -fdata-sections
  LDFLAGS += -Wl,--gc-sections
endif

# Static link — recommended for init systems
LDFLAGS += -static

# musl/uclibc: nothing extra needed
# glibc: -static pulls in libc.a automatically

# --- libc-specific tweaks ----------------------------------------------------
ifeq ($(LIBC),musl)
  # musl-gcc wrapper sets sysroot automatically
  # If using a generic cross-gcc, point sysroot manually:
  # CFLAGS  += --sysroot=/path/to/musl-sysroot
endif

ifeq ($(LIBC),uclibc)
  # uclibc may lack some POSIX-2008 extensions; fall back gracefully
  CFLAGS += -D_BSD_SOURCE
endif

# --- Rules -------------------------------------------------------------------
.PHONY: all clean matrix install strip info

all: $(BIN)

$(OUTDIR):
	mkdir -p $(OUTDIR)

$(OUTDIR)/%.o: $(SRCDIR)/%.c | $(OUTDIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BIN): $(OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) $^ -o $@
	@echo "  LINK $@"
	@$(SIZE) $@ 2>/dev/null || true

strip: $(BIN)
	$(STRIP) --strip-all $(BIN)
	@ls -lh $(BIN)

install: strip
	install -D -m 0755 $(BIN) $(DESTDIR)/sbin/init
	@echo "installed to $(DESTDIR)/sbin/init"

clean:
	rm -rf out/

info:
	@echo "TARGET  = $(TARGET)"
	@echo "CC      = $(CC)"
	@echo "CFLAGS  = $(CFLAGS)"
	@echo "LDFLAGS = $(LDFLAGS)"

# --- Matrix build: all supported targets ------------------------------------
#
# 'make matrix' tries to build every combination.  Targets whose
# cross-compiler is not installed will fail gracefully.
#
MATRIX_TARGETS := \
  x86-glibc      x86-musl      x86-uclibc      x86-dietlibc   \
  x86_64-glibc   x86_64-musl   x86_64-uclibc   x86_64-dietlibc \
  aarch64-glibc  aarch64-musl  aarch64-uclibc  aarch64-dietlibc \
  armhf-glibc    armhf-musl    armhf-uclibc    armhf-dietlibc  \
  armv7-glibc    armv7-musl    armv7-uclibc    armv7-dietlibc  \
  loongarch64-glibc loongarch64-musl           \
  ppc64le-glibc  ppc64le-musl  ppc64le-dietlibc \
  riscv64-glibc  riscv64-musl                 \
  s390x-glibc    s390x-musl    s390x-dietlibc

matrix:
	@failed=""; \
	for t in $(MATRIX_TARGETS); do \
	  arch=$$(echo $$t | cut -d- -f1); \
	  libc=$$(echo $$t | cut -d- -f2); \
	  echo ">>> Building $$t ..."; \
	  if $(MAKE) --no-print-directory ARCH=$$arch LIBC=$$libc 2>/dev/null; then \
	    echo "    OK: $$t"; \
	  else \
	    echo "    SKIP (no toolchain): $$t"; \
	    failed="$$failed $$t"; \
	  fi; \
	done; \
	echo ""; \
	echo "=== Matrix build complete ==="; \
	[ -z "$$failed" ] && echo "All targets built." || echo "Skipped (no toolchain):$$failed"
