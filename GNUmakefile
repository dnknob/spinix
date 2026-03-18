.SUFFIXES:

override OUTPUT := spinix

TOOLCHAIN_PREFIX ?=

ifeq ($(TOOLCHAIN_PREFIX),)
CC := gcc
LD := ld
AS := as
else
CC := $(TOOLCHAIN_PREFIX)gcc
LD := $(TOOLCHAIN_PREFIX)ld
AS := $(TOOLCHAIN_PREFIX)as
endif

CFLAGS   := -g -O2 -pipe
CPPFLAGS :=
LDFLAGS  :=

override CFLAGS += \
    -Wall \
    -Wextra \
    -std=gnu11 \
    -ffreestanding \
    -fno-stack-protector \
    -fno-stack-check \
    -fno-lto \
    -fno-PIC \
    -ffunction-sections \
    -fdata-sections \
    -m64 \
    -march=x86-64 \
    -mabi=sysv \
    -mno-80387 \
    -mno-mmx \
    -mno-sse \
    -mno-sse2 \
    -mno-red-zone \
    -mcmodel=kernel

override CPPFLAGS := \
    -I src \
    -I src/include \
    -MMD \
    -MP

override LDFLAGS += \
    -m elf_x86_64 \
    -nostdlib \
    -static \
    -z max-page-size=0x1000 \
    --gc-sections \
    -T src/linker.lds

USER_LDFLAGS := \
    -m elf_x86_64 \
    -static \
    -nostdlib \
    -Ttext=0x400000

override SRCFILES  := $(shell find src -type f | sort)
override CFILES    := $(filter %.c,$(SRCFILES))
override ASFILES   := $(filter %.S,$(SRCFILES))
override GASFILES  := $(filter %.s,$(SRCFILES))
override NASMFILES := $(filter %.asm,$(SRCFILES))

override OBJ  := $(addprefix obj/,$(CFILES:.c=.c.o) $(ASFILES:.S=.S.o) \
                  $(NASMFILES:.asm=.asm.o) $(GASFILES:.s=.s.o))
override DEPS := $(OBJ:.o=.d)

CRT0_SRC := user/crt0.s
CRT0_OBJ := obj/user/crt0.o

USER_SFILES := $(filter-out $(CRT0_SRC), $(wildcard user/*.s))
USER_CFILES := $(wildcard user/*.c)
USER_S_BINS := $(patsubst user/%.s, bin/%, $(USER_SFILES))
USER_C_BINS := $(patsubst user/%.c, bin/%, $(USER_CFILES))
USER_BINS   := $(USER_S_BINS) $(USER_C_BINS)

EMBED_OBJS := $(patsubst bin/%, obj/embed/%.o, $(USER_BINS))

.PHONY: all
all: bin/$(OUTPUT)

-include $(DEPS)

$(CRT0_OBJ): $(CRT0_SRC)
	mkdir -p $(dir $@)
	$(AS) --64 $< -o $@

bin/%: user/%.s $(CRT0_OBJ)
	mkdir -p bin obj/user
	$(AS) --64 $< -o obj/user/$*.s.o
	$(LD) $(USER_LDFLAGS) $(CRT0_OBJ) obj/user/$*.s.o -o $@

obj/user/%.c.o: user/%.c
	mkdir -p $(dir $@)
	gcc -ffreestanding -nostdlib -nostdinc -static \
	    -mno-red-zone -fno-stack-protector \
	    -O2 -m64 -I user/include \
	    -c $< -o $@

bin/%: obj/user/%.c.o $(CRT0_OBJ)
	mkdir -p bin
	$(LD) $(USER_LDFLAGS) $(CRT0_OBJ) $< -o $@

obj/embed/%.o: bin/%
	mkdir -p $(dir $@)
	objcopy -I binary -O elf64-x86-64 -B i386:x86-64 \
	    --rename-section .data=.rodata,alloc,load,readonly,data,contents \
	    $< $@

bin/$(OUTPUT): $(OBJ) $(EMBED_OBJS)
	mkdir -p bin
	$(LD) $(LDFLAGS) $(OBJ) $(EMBED_OBJS) -o $@

obj/%.c.o: %.c
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

obj/%.S.o: %.S
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

obj/%.s.o: %.s
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

obj/%.asm.o: %.asm
	mkdir -p $(dir $@)
	nasm -f elf64 -Wall $< -o $@

.PHONY: limine
limine:
	if [ ! -d limine ]; then \
		git clone https://codeberg.org/Limine/Limine.git \
		    --branch=v10.x-binary --depth=1 limine; \
	fi
	make -C limine

.PHONY: iso
iso: all limine
	rm -rf iso_root
	mkdir -p iso_root/boot/limine iso_root/EFI/BOOT
	cp bin/$(OUTPUT)            iso_root/boot/
	cp src/limine.conf          iso_root/boot/limine/
	cp limine/limine-bios.sys   iso_root/boot/limine/
	cp limine/limine-bios-cd.bin  iso_root/boot/limine/
	cp limine/limine-uefi-cd.bin  iso_root/boot/limine/
	cp limine/BOOTX64.EFI       iso_root/EFI/BOOT/
	cp limine/BOOTIA32.EFI      iso_root/EFI/BOOT/
	xorriso -as mkisofs -R -r -J \
	    -b boot/limine/limine-bios-cd.bin \
	    -no-emul-boot -boot-load-size 4 -boot-info-table \
	    -hfsplus -apm-block-size 2048 \
	    --efi-boot boot/limine/limine-uefi-cd.bin \
	    -efi-boot-part --efi-boot-image \
	    --protective-msdos-label \
	    iso_root -o image.iso
	./limine/limine bios-install image.iso

.PHONY: run
run: iso
	qemu-system-x86_64 -enable-kvm -cpu host -smp 3 -m 1G -M q35 \
		-drive format=raw,file=image.iso -serial stdio

.PHONY: clean
clean:
	rm -rf obj bin iso_root image.iso limine
