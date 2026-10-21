# Sartax OS - budowanie (uruchamiac w WSL: make)
VERSION := $(shell grep 'define SARTAX_VERSION ' kernel/kernel.h | cut -d'"' -f2)
B := build
CC := gcc
NASM := nasm
PY := python3

CFLAGS := -std=gnu11 -O2 -ffreestanding -fno-stack-protector -fno-stack-check -fno-pic -fno-pie \
	-mno-red-zone -mgeneral-regs-only -mcmodel=small -fno-asynchronous-unwind-tables -fno-unwind-tables \
	-fcf-protection=none -fno-strict-aliasing -fno-tree-loop-distribute-patterns \
	-Wall -Wno-unused-parameter -Wno-sign-compare -Wno-unused-function -Wno-address-of-packed-member \
	-Ikernel -Ikernel/include -Ithird_party/BearSSL/inc -MMD
BRDEFS := -DBR_USE_URANDOM=0 -DBR_USE_GETENTROPY=0 -DBR_USE_UNIX_TIME=0 -DBR_USE_WIN32_RAND=0 \
	-DBR_USE_WIN32_TIME=0 -DBR_RDRAND=0 -DBR_AES_X86NI=0 -DBR_SSE2=0 -DBR_POWER8=0 \
	-DBR_INT128=1 -DBR_LE_UNALIGNED=1 -DBR_64=1 -DBR_ENABLE_INTRINSICS=0
BRFLAGS := $(CFLAGS) -Ithird_party/BearSSL/src -w $(BRDEFS)

KSRC := $(wildcard kernel/*.c) $(wildcard kernel/net/*.c) $(wildcard kernel/apps/*.c)
GEN := kernel/gen/fontdata.c kernel/gen/icondata.c kernel/gen/trustanchors.c kernel/gen/initfs.c
BRSRC := $(shell find third_party/BearSSL/src -name '*.c')
KOBJ := $(patsubst %.c,$(B)/%.o,$(KSRC) $(GEN))
BROBJ := $(patsubst third_party/BearSSL/src/%.c,$(B)/bearssl/%.o,$(BRSRC))

all: $(B)/sartax.iso

$(B)/%.o: %.c
	@mkdir -p $(dir $@)
	@echo "  CC  $<"
	@$(CC) $(CFLAGS) -c $< -o $@

$(B)/bearssl/%.o: third_party/BearSSL/src/%.c
	@mkdir -p $(dir $@)
	@$(CC) $(BRFLAGS) -c $< -o $@

$(B)/mbr.bin: boot/mbr.asm
	@mkdir -p $(B)
	$(NASM) -f bin $< -o $@
$(B)/cdboot.bin: boot/cdboot.asm
	@mkdir -p $(B)
	$(NASM) -f bin $< -o $@
$(B)/stage2.bin: boot/stage2.asm
	@mkdir -p $(B)
	$(NASM) -f bin $< -o $@

$(B)/entry.o: kernel/entry.asm
	@mkdir -p $(B)
	$(NASM) -f elf64 $< -o $@
$(B)/blobs.o: kernel/blobs.asm $(B)/mbr.bin $(B)/stage2.bin
	$(NASM) -f elf64 $< -o $@

kernel/gen/fontdata.c: tools/mkfont.py
	@mkdir -p kernel/gen
	$(PY) $< $@
kernel/gen/icondata.c: tools/mkicons.py
	@mkdir -p kernel/gen
	$(PY) $< $@
kernel/gen/initfs.c: tools/mkinitfs.py $(shell find initfs -type d)
	@mkdir -p kernel/gen
	$(PY) $< $@
kernel/gen/trustanchors.c:
	@mkdir -p kernel/gen
	$(MAKE) -C third_party/BearSSL build/brssl >/dev/null
	( echo '#include "bearssl.h"'; third_party/BearSSL/build/brssl ta /etc/ssl/certs/ca-certificates.crt 2>/dev/null; \
	  echo 'const br_x509_trust_anchor *g_TAs = TAs; const size_t g_TAs_num = TAs_NUM;' ) > $@

$(B)/kernel.elf: $(B)/entry.o $(B)/blobs.o $(KOBJ) $(BROBJ) kernel/linker.ld
	@echo "  LD  $@"
	@$(CC) -nostdlib -static -no-pie -Wl,-T,kernel/linker.ld -Wl,--no-warn-rwx-segments -o $@ \
		$(B)/entry.o $(B)/blobs.o $(KOBJ) $(BROBJ) -lgcc

$(B)/kernel.bin: $(B)/kernel.elf tools/mkimage.py
	objcopy -O binary $< $@.tmp
	$(PY) tools/mkimage.py kernel $@.tmp $@ $(VERSION)

$(B)/sartax.iso: $(B)/cdboot.bin $(B)/stage2.bin $(B)/kernel.bin tools/mkimage.py
	rm -rf $(B)/iso && mkdir -p $(B)/iso/boot
	$(PY) tools/mkimage.py cd $(B)/cdboot.bin $(B)/stage2.bin $(B)/kernel.bin $(B)/iso/boot/sartax.img
	cp $(B)/kernel.bin $(B)/iso/boot/kernel.bin
	printf 'Sartax OS %s\r\nWloz ta plyte do maszyny wirtualnej i uruchom.\r\n' $(VERSION) > $(B)/iso/CZYTAJ.TXT
	xorriso -as mkisofs -quiet -R -J -V SARTAX_OS -b boot/sartax.img -no-emul-boot -boot-load-size 4 \
		-boot-info-table -o $@ $(B)/iso
	@ls -la $@

# ---- programy .sap (SDK) ----
SAPFLAGS := -std=gnu11 -O2 -ffreestanding -fpie -fvisibility=hidden -mno-red-zone -mgeneral-regs-only -fno-builtin \
	-fno-stack-protector -fcf-protection=none -fno-asynchronous-unwind-tables -fno-tree-loop-distribute-patterns -Wall
SAPS := $(patsubst apps/%/,$(B)/apps/%.sap,$(wildcard apps/*/))

$(B)/apps/%.sap: apps/%/*.c sdk/sartax.h sdk/saplib.c
	@mkdir -p $(B)/apps/obj
	$(CC) $(SAPFLAGS) -c $(filter %.c,$(filter apps/%,$^)) -o $(B)/apps/obj/$*.o
	$(CC) $(SAPFLAGS) -c sdk/saplib.c -o $(B)/apps/obj/saplib.o
	ld -shared -Bsymbolic --no-undefined -s -e sap_main -z noexecstack -o $@ $(B)/apps/obj/$*.o $(B)/apps/obj/saplib.o

apps: $(SAPS)

run: $(B)/sartax.iso
	qemu-system-x86_64 -m 512 -cdrom $< -serial stdio -boot d

clean:
	rm -rf $(B) kernel/gen

-include $(KOBJ:.o=.d)
.PHONY: all run clean apps
