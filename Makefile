NASM ?= nasm
BUILD := build

.PHONY: all clean run test

all: $(BUILD)/hypeai-os.img

$(BUILD)/stage1.bin: boot/stage1.asm
	$(NASM) -f bin $< -o $@

$(BUILD)/stage2.bin: boot/stage2.asm
	$(NASM) -f bin $< -o $@

$(BUILD)/kernel.bin: kernel/kernel.asm
	$(NASM) -f bin $< -o $@

$(BUILD)/hypeai-os.img: $(BUILD)/stage1.bin $(BUILD)/stage2.bin $(BUILD)/kernel.bin tools/mkimage.py
	python3 tools/mkimage.py

run: all
	qemu-system-x86_64 -drive format=raw,file=$(BUILD)/hypeai-os.img -serial stdio -display none -no-reboot

test: all
	./tools/test-qemu.sh

clean:
	rm -f $(BUILD)/*.bin $(BUILD)/*.img
