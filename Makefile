NASM ?= nasm
CXX ?= g++
LD ?= ld
BUILD := build

CXXFLAGS := -std=c++20 -O2 -ffreestanding -fno-exceptions -fno-rtti \
	-fno-use-cxa-atexit -fno-threadsafe-statics -fno-stack-protector \
	-fno-pic -fno-pie -mno-red-zone -m64 -mcmodel=large -Wall -Wextra -I.

KERNEL_OBJECTS := $(BUILD)/entry.o $(BUILD)/main.o
KERNEL_HEADERS := $(shell find hcpp kernel -name '*.hpp' 2>/dev/null)

.PHONY: all clean run test

all: $(BUILD)/hypeai-os.img

$(BUILD)/stage1.bin: boot/stage1.asm
	$(NASM) -f bin $< -o $@

$(BUILD)/stage2.bin: boot/stage2.asm
	$(NASM) -f bin $< -o $@

$(BUILD)/entry.o: kernel/entry.asm
	$(NASM) -f elf64 $< -o $@

$(BUILD)/main.o: kernel/main.cpp $(KERNEL_HEADERS)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/kernel.bin: $(KERNEL_OBJECTS) kernel/linker.ld
	$(LD) -nostdlib -z noexecstack -T kernel/linker.ld -o $@ $(KERNEL_OBJECTS)

$(BUILD)/hypeai-os.img: $(BUILD)/stage1.bin $(BUILD)/stage2.bin $(BUILD)/kernel.bin tools/mkimage.py
	python3 tools/mkimage.py

run: all
	qemu-system-x86_64 -drive format=raw,file=$(BUILD)/hypeai-os.img -serial stdio -display none -no-reboot

test: all
	./tools/test-qemu.sh

clean:
	rm -f $(BUILD)/*.bin $(BUILD)/*.o $(BUILD)/*.img
