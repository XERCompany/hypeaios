NASM ?= nasm
TRIPLE := x86_64-unknown-none-elf
CXX ?= clang++
ifeq ($(origin CXX),default)
CXX := clang++
endif
ifeq ($(origin CC),default)
CC := clang
endif
ifeq ($(origin LD),default)
LD := $(CXX) --target=$(TRIPLE) -fuse-ld=lld
endif
LD ?= $(CXX) --target=$(TRIPLE) -fuse-ld=lld
BUILD := build

CXXFLAGS := -I$(BUILD) --target=$(TRIPLE) -std=c++20 -Os -ffreestanding -fno-exceptions -fno-rtti \
	-fno-use-cxa-atexit -fno-threadsafe-statics -fno-stack-protector \
	-fno-pic -fno-pie -mno-red-zone -m64 -mcmodel=large -Wall -Wextra -I. \
	-Ithird_party/sqlite

CFLAGS := --target=$(TRIPLE) -Os -ffreestanding -fno-builtin \
	-fno-stack-protector -fno-pic -fno-pie -mno-red-zone -m64 -mcmodel=large \
	-w -isystem third_party/sqlite/freestanding -Ithird_party/sqlite \
	-DSQLITE_OS_OTHER=1 -DSQLITE_THREADSAFE=0 -DNDEBUG \
	-DSQLITE_OMIT_LOAD_EXTENSION -DSQLITE_OMIT_WAL -DSQLITE_OMIT_UTF16 \
	-DSQLITE_OMIT_FOREIGN_KEY -DSQLITE_DEFAULT_MEMSTATUS=0

CC_SOURCES := $(shell find hcpp kernel -name '*.cc' 2>/dev/null | sort)
CC_OBJECTS := $(patsubst %.cc,$(BUILD)/%.o,$(CC_SOURCES))
C_SOURCES := $(shell find third_party -name '*.c' 2>/dev/null | sort)
C_OBJECTS := $(patsubst %.c,$(BUILD)/%.o,$(C_SOURCES))
ASM_SOURCES := $(shell find kernel \( -name '*.asm' -o -name '*.S' \) ! -path 'kernel/entry.asm' 2>/dev/null | sort)
ASM_OBJECTS := $(patsubst %.asm,$(BUILD)/%.o,$(patsubst %.S,$(BUILD)/%.o,$(ASM_SOURCES)))
FONT_OBJECTS := $(BUILD)/fonts/hypeai-8x16_hpf.o
KERNEL_OBJECTS := $(BUILD)/entry.o $(CC_OBJECTS) $(C_OBJECTS) $(ASM_OBJECTS) $(FONT_OBJECTS)
KERNEL_HEADERS := $(shell find hcpp kernel third_party -name '*.hpp' -o -name '*.h' 2>/dev/null)

PYTHON ?= python3
FONT_ASSETS := $(BUILD)/fonts/hypeai-8x16_hpf.cc

# UEFI loader (BOOTX64.EFI): freestanding C++ compiled to COFF and linked
# by lld-link as an EFI application. No gnu-efi needed.
UEFI_CXXFLAGS := --target=x86_64-pc-windows-msvc -std=c++20 -Os \
	-ffreestanding -fno-exceptions -fno-rtti -fno-stack-protector \
	-fshort-wchar -mno-red-zone -m64 -Wall -Wextra -I.
LLD_LINK ?= lld-link
OVMF ?= C:/Program Files/qemu/share/edk2-x86_64-code.fd

.PHONY: all clean run test iso vdi usb uefi run-uefi

all: $(BUILD)/hypeai-os.img $(FONT_ASSETS)

$(BUILD):
	mkdir -p $@

$(BUILD)/stage1.bin: boot/stage1.asm | $(BUILD)
	$(NASM) -f bin $< -o $@

$(BUILD)/stage2.bin: boot/stage2.asm | $(BUILD)
	$(NASM) -f bin $< -o $@

$(BUILD)/entry.o: kernel/entry.asm | $(BUILD)
	$(NASM) -f elf64 $< -o $@

$(BUILD)/%.o: %.asm | $(BUILD)
	mkdir -p $(dir $@)
	$(NASM) -f elf64 $< -o $@

$(BUILD)/%.o: %.S | $(BUILD)
	mkdir -p $(dir $@)
	$(NASM) -f elf64 $< -o $@

$(BUILD)/version_generated.h: tools/version.py | $(BUILD)
	python3 tools/version.py >/dev/null

$(BUILD)/fonts/%_hpf.cc: tools/fonts/%.psfu tools/fontc.py | $(BUILD)
	mkdir -p $(dir $@)
	$(PYTHON) tools/fontc.py $< --name $* --hpf $(BUILD)/fonts/$*.hpf --cc $@

$(BUILD)/%.o: %.cc $(KERNEL_HEADERS) $(BUILD)/version_generated.h | $(BUILD)
	mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/%.o: %.c $(KERNEL_HEADERS) | $(BUILD)
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/fonts/%.o: $(BUILD)/fonts/%.cc | $(BUILD)
	mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/kernel.bin: $(KERNEL_OBJECTS) kernel/linker.ld
	$(LD) -nostdlib -z noexecstack -T kernel/linker.ld -Wl,-Map,$(BUILD)/kernel.map -o $@ $(KERNEL_OBJECTS)

PAYLOAD_FILES := $(wildcard docs/wiki/*.md docs/research/*.md) cursor.png tools/Montserrat.ttf \
	$(wildcard payload/wallpapers/*.png payload/help/*.txt payload/fonts/* payload/music/*.mp3)

$(BUILD)/hypeai-os.img: $(BUILD)/stage1.bin $(BUILD)/stage2.bin $(BUILD)/kernel.bin tools/mkimage.py $(PAYLOAD_FILES)
	python3 tools/mkimage.py

run: all
	qemu-system-x86_64 -drive format=raw,file=$(BUILD)/hypeai-os.img \
		-device intel-hda -device hda-duplex,audiodev=snd0 -audiodev none,id=snd0 \
		-device sb16,audiodev=snd0 -device adlib,audiodev=snd0 \
		-device AC97,audiodev=snd0 \
		-device ES1370,audiodev=snd0 \
		-serial stdio -display none -no-reboot

$(BUILD)/hypeai-os.iso: $(BUILD)/hypeai-os.img tools/mkiso.py $(BUILD)/version_generated.h
	python3 tools/mkiso.py

iso: $(BUILD)/hypeai-os.iso

$(BUILD)/uefi/bootx64.o: boot/uefi/bootx64.cc boot/uefi/efi.hpp kernel/boot_info.hpp | $(BUILD)
	mkdir -p $(dir $@)
	$(CXX) $(UEFI_CXXFLAGS) -c $< -o $@

$(BUILD)/uefi/BOOTX64.EFI: $(BUILD)/uefi/bootx64.o
	$(LLD_LINK) -subsystem:efi_application -entry:efi_main -nodefaultlib -out:$@ $<

$(BUILD)/hypeai-os-uefi.img: $(BUILD)/uefi/BOOTX64.EFI $(BUILD)/kernel.bin \
		tools/mkesp.py tools/mkimage.py $(PAYLOAD_FILES)
	$(PYTHON) tools/mkesp.py

# GPT disk: FAT32 ESP (EFI/BOOT/BOOTX64.EFI + EFI/HYPEAI/KERNEL.BIN) plus
# the HYPEDATA payload at the fixed LBA 4129 the kernel block layer reads.
uefi: $(BUILD)/hypeai-os-uefi.img

# On Windows QEMU cannot open images under the Cyrillic repo path — copy
# the image to an ASCII path first (see docs/wiki/build-and-run.md), and
# -bios does not work there, so OVMF goes on a pflash drive.
run-uefi: uefi
	qemu-system-x86_64 -drive if=pflash,format=raw,readonly=on,file="$(OVMF)" \
		-drive format=raw,file=$(BUILD)/hypeai-os-uefi.img \
		-serial stdio -display none -no-reboot

$(BUILD)/hypeai-os.vdi: $(BUILD)/hypeai-os.img
	@if command -v VBoxManage >/dev/null 2>&1; then \
		VBoxManage convertfromraw --format VDI $(BUILD)/hypeai-os.img $(BUILD)/hypeai-os.vdi; \
	elif [ -x "/c/Program Files/Oracle/VirtualBox/VBoxManage.exe" ]; then \
		"/c/Program Files/Oracle/VirtualBox/VBoxManage.exe" convertfromraw --format VDI \
			"`cygpath -w $(BUILD)/hypeai-os.img`" "`cygpath -w $(BUILD)/hypeai-os.vdi`"; \
	else \
		echo "VBoxManage not found - attach build/hypeai-os.img as raw disk or install VirtualBox"; \
		exit 1; \
	fi

vdi: $(BUILD)/hypeai-os.vdi

usb: all
	@echo "Write build/hypeai-os.img to a USB stick (all data on it will be erased):"
	@echo "  Windows: Rufus -> select image -> 'DD Image' mode -> START"
	@echo "          or balenaEtcher -> Flash from file -> hypeai-os.img"
	@echo "  Linux:   dd if=build/hypeai-os.img of=/dev/sdX bs=4M status=progress conv=fsync"
	@echo "Boot the stick with BIOS/CSM (Legacy) mode enabled."
	@echo "For UEFI-only machines build 'make uefi' and flash build/hypeai-os-uefi.img"
	@echo "instead (Secure Boot must be off - the loader is unsigned)."

test: all
	./tools/test-qemu.sh

embedfont:
	$(PYTHON) tools/embed_ttf.py

embedcursor:
	$(PYTHON) tools/embed_cursor.py

clean:
	rm -rf $(BUILD)
	mkdir -p $(BUILD)
