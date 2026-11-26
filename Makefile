NASM ?= nasm
CXX ?= g++
LD ?= ld
BUILD := build

CXXFLAGS := -I$(BUILD) -std=c++20 -Os -ffreestanding -fno-exceptions -fno-rtti \
	-fno-use-cxa-atexit -fno-threadsafe-statics -fno-stack-protector \
	-fno-pic -fno-pie -mno-red-zone -m64 -mcmodel=large -Wall -Wextra -I.

CC_SOURCES := $(shell find hcpp kernel -name '*.cc' 2>/dev/null | sort)
CC_OBJECTS := $(patsubst %.cc,$(BUILD)/%.o,$(CC_SOURCES))
ASM_SOURCES := $(shell find kernel -name '*.asm' ! -path 'kernel/entry.asm' 2>/dev/null | sort)
ASM_OBJECTS := $(patsubst %.asm,$(BUILD)/%.o,$(ASM_SOURCES))
KERNEL_OBJECTS := $(BUILD)/entry.o $(CC_OBJECTS) $(ASM_OBJECTS)
KERNEL_HEADERS := $(shell find hcpp kernel -name '*.hpp' 2>/dev/null)

PYTHON ?= python3
FONT_ASSETS := $(BUILD)/fonts/hypeai-8x16_hpf.cc

.PHONY: all clean run test

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

$(BUILD)/version_generated.h: tools/version.py | $(BUILD)
	python3 tools/version.py >/dev/null

$(BUILD)/fonts/%_hpf.cc: tools/fonts/%.psfu tools/fontc.py | $(BUILD)
	mkdir -p $(dir $@)
	$(PYTHON) tools/fontc.py $< --name $* --hpf $(BUILD)/fonts/$*.hpf --cc $@

$(BUILD)/%.o: %.cc $(KERNEL_HEADERS) $(BUILD)/version_generated.h | $(BUILD)
	mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/kernel.bin: $(KERNEL_OBJECTS) kernel/linker.ld
	$(LD) -nostdlib -z noexecstack -T kernel/linker.ld -o $@ $(KERNEL_OBJECTS)

$(BUILD)/hypeai-os.img: $(BUILD)/stage1.bin $(BUILD)/stage2.bin $(BUILD)/kernel.bin tools/mkimage.py
	python3 tools/mkimage.py

run: all
	qemu-system-x86_64 -drive format=raw,file=$(BUILD)/hypeai-os.img -serial stdio -display none -no-reboot

$(BUILD)/hypeai-os.iso: $(BUILD)/hypeai-os.img tools/mkiso.py $(BUILD)/version_generated.h
	python3 tools/mkiso.py

iso: $(BUILD)/hypeai-os.iso

test: all
	./tools/test-qemu.sh

clean:
	rm -rf $(BUILD)
	mkdir -p $(BUILD)
