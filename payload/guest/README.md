# Guest initramfs payload

`initramfs.cpio` in this directory is embedded into the kernel image by
`kernel/kvm/initramfsdata.S` (`incbin`), which exports the symbols
`initramfs_data` / `initramfs_end` for the KVM module to hand to a guest.

## The committed file is a placeholder

`initramfs.cpio` ships as a small marker blob (starting with the CPIO
newc magic `070701`) purely so `make` never fails on a missing file.
It is not a bootable archive.

## Replacing it with a real initramfs

Build or copy an uncompressed CPIO archive in `newc` format over the
placeholder — e.g.:

```sh
cd guest-rootfs/
find . | cpio -o -H newc > ../../payload/guest/initramfs.cpio
```

Then rebuild (`make iso`). The KVM module reads the embedded region via
`kvm_initramfs()` in `kernel/kvm/kvm.h`. Keep the archive uncompressed:
the kernel side does not decompress.
