#!/usr/bin/env python3
import pathlib, subprocess
root=pathlib.Path(__file__).resolve().parent.parent
build=root/'build'; build.mkdir(exist_ok=True)
try: n=subprocess.check_output(['git','rev-list','--count','HEAD'],cwd=root,text=True).strip()
except Exception: n='0'
try: sha=subprocess.check_output(['git','rev-parse','--short','HEAD'],cwd=root,text=True).strip()
except Exception: sha='unknown'
version=f'0.1.{n}'
(build/'version.txt').write_text(f'HypeAI OS {version}+{sha}\n')
(build/'version_generated.h').write_text(f'#pragma once\n#define HYPEAI_VERSION "{version}"\n#define HYPEAI_VERSION_FULL "{version}+{sha}"\n')
print(version+'+'+sha)
