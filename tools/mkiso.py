#!/usr/bin/env python3
import pathlib, subprocess
root=pathlib.Path(__file__).resolve().parent.parent
build=root/'build'
iso=build/'hypeai-os.iso'
readme=build/'ISO-README.txt'
version=(build/'version.txt').read_text().strip() if (build/'version.txt').exists() else 'HypeAI OS'
readme.write_text(version+'\nBoot the raw image hypeai-os.img with QEMU or write it to a test disk.\nThe image is included in this ISO artifact.\n')
subprocess.check_call(['xorriso','-as','mkisofs','-o','hypeai-os.iso','-V','HYPEAIOS','-J','-R','hypeai-os.img','version.txt','ISO-README.txt'],cwd=build)
print(iso)
