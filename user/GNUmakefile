.SUFFIXES:

# ─── Toolchain ────────────────────────────────────────────────────────────────

TOOLCHAIN_PREFIX ?=

ifeq ($(TOOLCHAIN_PREFIX),)
CC := gcc
AS := as
LD := ld
else
CC := $(TOOLCHAIN_PREFIX)gcc
AS := $(TOOLCHAIN_PREFIX)as
LD := $(TOOLCHAIN_PREFIX)ld
endif

# ─── Flags ────────────────────────────────────────────────────────────────────

override CFLAGS := \
    -g -O2 -pipe \
    -Wall \
    -Wextra \
    -std=gnu11 \
    -ffreestanding \
    -nostdlib \
    -nostdinc \
    -fno-stack-protector \
    -fno-builtin \
    -fno-lto \
    -fno-PIC \
    -ffunction-sections \
    -fdata-sections \
    -m64 \
    -march=x86-64 \
    -mabi=sysv \
    -mno-red-zone \
    -mno-sse \
    -mno-sse2 \
    -mno-mmx

# src/include (one level up) provides the freestanding stddef.h / stdint.h /
# stdbool.h that already exist in the project.
override CPPFLAGS := \
    -I ../src/include \
    -I libc/include \
    -I include \
    -MMD \
    -MP

override LDFLAGS := \
    -m elf_x86_64 \
    -static \
    -nostdlib \
    -Ttext=0x400000 \
    --gc-sections

# ─── crt0 ────────────────────────────────────────────────────────────────────

CRT0_SRC := crt0.s
CRT0_OBJ := obj/crt0.o

# ─── libc ────────────────────────────────────────────────────────────────────

LIBC_CSRC := $(shell find libc -name '*.c' 2>/dev/null | sort)
LIBC_SSRC := $(shell find libc -name '*.s' 2>/dev/null | sort)
LIBC_OBJS := $(addprefix obj/, $(LIBC_CSRC:.c=.c.o) $(LIBC_SSRC:.s=.s.o))
LIBC_AR   := obj/libc.a

# ─── User programs (only .c / .s directly in this directory) ─────────────────

PROG_CSRC := $(wildcard *.c)
PROG_SSRC := $(filter-out $(CRT0_SRC), $(wildcard *.s))
PROG_BINS := $(addprefix bin/, $(PROG_CSRC:.c=) $(PROG_SSRC:.s=))

DEPS := $(shell find obj -name '*.d' 2>/dev/null)

# ─── Default target ───────────────────────────────────────────────────────────

.PHONY: all
all: $(PROG_BINS)

-include $(DEPS)

# ─── crt0 ────────────────────────────────────────────────────────────────────

$(CRT0_OBJ): $(CRT0_SRC)
	mkdir -p $(dir $@)
	$(AS) --64 $< -o $@

# ─── libc ────────────────────────────────────────────────────────────────────

obj/libc/%.c.o: libc/%.c
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

obj/libc/%.s.o: libc/%.s
	mkdir -p $(dir $@)
	$(AS) --64 $< -o $@

ifneq ($(LIBC_OBJS),)
$(LIBC_AR): $(LIBC_OBJS)
	mkdir -p $(dir $@)
	$(AR) rcs $@ $^
LINK_LIBC := $(LIBC_AR)
else
LINK_LIBC :=
endif

# ─── User programs from C ─────────────────────────────────────────────────────

obj/%.c.o: %.c
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

bin/%: obj/%.c.o $(CRT0_OBJ) $(LINK_LIBC)
	mkdir -p bin
	$(LD) $(LDFLAGS) $(CRT0_OBJ) $< $(LINK_LIBC) -o $@

# ─── User programs from assembly ─────────────────────────────────────────────

obj/%.s.o: %.s
	mkdir -p $(dir $@)
	$(AS) --64 $< -o $@

bin/%: obj/%.s.o $(CRT0_OBJ) $(LINK_LIBC)
	mkdir -p bin
	$(LD) $(LDFLAGS) $(CRT0_OBJ) $< $(LINK_LIBC) -o $@

# ─── Run a single program via QEMU user-mode emulator ────────────────────────
#   make run PROG=test

.PHONY: run
run: bin/$(PROG)
	qemu-x86_64 $<

# ─── Clean ───────────────────────────────────────────────────────────────────

.PHONY: clean
clean:
	rm -rf obj bin
