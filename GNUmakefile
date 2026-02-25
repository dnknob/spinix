.SUFFIXES:

override OUTPUT := spinix

TOOLCHAIN_PREFIX ?=

ifeq ($(TOOLCHAIN_PREFIX),)
CC := gcc
LD := ld
else
CC := $(TOOLCHAIN_PREFIX)gcc
LD := $(TOOLCHAIN_PREFIX)ld
endif

CFLAGS := -g -O2 -pipe
CPPFLAGS :=
LDFLAGS :=
NASMFLAGS := -g

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

override NASMFLAGS := \
    -f elf64 \
    -Wall

override SRCFILES := $(shell find src -type f | sort)
override CFILES := $(filter %.c,$(SRCFILES))
override ASFILES := $(filter %.S,$(SRCFILES))
override GASFILES := $(filter %.s,$(SRCFILES))
override NASMFILES := $(filter %.asm,$(SRCFILES))

override OBJ := $(addprefix obj/,$(CFILES:.c=.c.o) $(ASFILES:.S=.S.o) $(NASMFILES:.asm=.asm.o) $(GASFILES:.s=.s.o))
override DEPS := $(OBJ:.o=.d)

.PHONY: all
all: bin/$(OUTPUT)

-include $(DEPS)

bin/$(OUTPUT): $(OBJ)
	mkdir -p bin
	$(LD) $(LDFLAGS) $(OBJ) -o $@

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
	nasm $(NASMFLAGS) $< -o $@

.PHONY: limine
limine:
	if [ ! -d limine ]; then \
		git clone https://codeberg.org/Limine/Limine.git --branch=v10.x-binary --depth=1 limine; \
	fi
	make -C limine

.PHONY: iso
iso: all limine
	rm -rf iso_root
	mkdir -p iso_root/boot
	mkdir -p iso_root/boot/limine
	mkdir -p iso_root/EFI/BOOT

	cp bin/$(OUTPUT) iso_root/boot/
	cp src/limine.conf iso_root/boot/limine/
	cp limine/limine-bios.sys iso_root/boot/limine/
	cp limine/limine-bios-cd.bin iso_root/boot/limine/
	cp limine/limine-uefi-cd.bin iso_root/boot/limine/

	cp limine/BOOTX64.EFI iso_root/EFI/BOOT/
	cp limine/BOOTIA32.EFI iso_root/EFI/BOOT/

	xorriso -as mkisofs -R -r -J \
	    -b boot/limine/limine-bios-cd.bin \
	    -no-emul-boot -boot-load-size 4 -boot-info-table \
	    -hfsplus \
	    -apm-block-size 2048 \
	    --efi-boot boot/limine/limine-uefi-cd.bin \
	    -efi-boot-part --efi-boot-image \
	    --protective-msdos-label \
	    iso_root -o image.iso

	./limine/limine bios-install image.iso

.PHONY: run
run: iso
	qemu-system-x86_64 -enable-kvm -cpu host -smp 3 -m 1G -M q35 -drive format=raw,file=image.iso -serial stdio

.PHONY: clean
clean:
	rm -rf obj bin iso_root image.iso limine

