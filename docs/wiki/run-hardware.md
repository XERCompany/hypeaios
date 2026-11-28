# Запуск HypeAI OS на реальном железе и в VirtualBox

Образ `build/hypeai-os.img` — сырой загрузочный диск (MBR + VBE 1024x768).
QEMU ему не нужен: та же картинка грузится с USB-флешки и в VirtualBox.

## Что уже работает везде (не зависит от QEMU)

| Подсистема | Путь |
|---|---|
| Видео | BIOS VBE mode 0x118 (1024x768x32 LFB) в `boot/stage2.asm` — универсально |
| Диск | ATA PIO (порты 0x1F0) — PIIX/ICH IDE у VirtualBox и реальных чипсетов |
| HYPEDATA | блоб на LBA 8258, монтируется с любого ATA-устройства |
| Аудио | Intel HDA / AC97 / ES1370 / SB16+OPL2 — покрывает весь выбор VirtualBox |
| Ввод | PS/2 клава+мышь (IRQ1/IRQ12, Intellimouse wheel) — эмулируется всеми VM; USB HID (boot protocol, клава+мышь) через xHCI — `kernel/usb/usbhid.cc`, маркеры `usb kbd=ok`/`usb mouse=ok` |
| Часы | CMOS RTC + PIT 100Hz |
| Реестр | PCI scan с bridge-рекурсией — устройства просто не найдутся, если их нет |

Отсутствующее железо помечается `*=absent` в serial и драйверы отключаются
graceful — паники не будет.

## USB-флешка (реальное железо)

1. Собери: `make iso` (или `make`) -> `build/hypeai-os.img` (~6 MB).
2. Запиши на флешку **в dd-режиме** (все данные на флешке будут стёрты):
   - Windows: Rufus -> Выбор образа -> режим **DD Image** -> Старт;
     либо balenaEtcher -> Flash from file -> `hypeai-os.img`.
   - Linux: `dd if=build/hypeai-os.img of=/dev/sdX bs=4M status=progress conv=fsync`
3. Загрузись с флешки. Требуется **BIOS/CSM (Legacy) boot** — UEFI-only
   режим пока не поддержан (stage1 — MBR-загрузчик).
4. Отладка: serial COM1 115200 — весь boot-log идёт туда
   (`boot fb=black`, `svc:*`, `ata model=...`).

`make usb` печатает эту шпаргалку.

## VirtualBox

1. Конвертируй образ: `make vdi` (нужен VBoxManage; ищется в PATH и в
   `C:\Program Files\Oracle\VirtualBox\`). Получится `build/hypeai-os.vdi`.
2. Новая VM:
   - Type: Other / Other Unknown (64-bit), >= 256 MB RAM.
   - **Storage**: контроллер **IDE PIIX3/PIIX4** (не SATA/AHCI — драйвер
     AHCI ещё не написан; ATA PIO ходит по 0x1F0). Подключи `hypeai-os.vdi`.
   - **Audio**: любой из Intel HDA / AC'97 / SoundBlaster 16 —
     драйверы есть под все три.
   - **Display**: VBoxVGA или VMSVGA — оба отдают VESA/VBE, нам достаточно.
   - **System -> Chipset: PIIX3**, PS/2 Mouse (таблетку не нужно — мы на PS/2).
3. Serial: в настройках VM порт COM1 -> вывод в файл/пайп — тот же
   диагностический поток, что `-serial stdio` у QEMU.

### Что в VirtualBox будет absent (ожидаемо)

- virtio-* устройств нет — `virtio`-стек не активируется.
- Bochs-dispi регистров нет — modeset останется VBE 1024x768 (re-modeset
  через dispi не нужен: разрешение уже выставлено stage2).
- Intel HDA присутствует при выборе "Intel HD Audio" — тогда ASIO-путь
  (`hasio lat<10ms ok`) работает как в QEMU.

## VMware/др.

- VMware: raw img -> `qemu-img convert -f raw -O vmdk` (qemu-img идёт с
  QEMU) или StarWind V2V; графика SVGA II отдаёт VBE — загрузится.
- Hyper-V (Gen1, IDE-диск): конверт raw->vhdx `qemu-img convert -O vhdx`;
  Gen2 (UEFI-only) не поддержан.
