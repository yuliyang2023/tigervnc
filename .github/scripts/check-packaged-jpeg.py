#!/usr/bin/env python3
"""Run the JPEG regression test against the libraries shipped in the app."""

import shutil
import subprocess
import sys
import tempfile
from pathlib import Path


source, app = map(Path, sys.argv[1:])
with tempfile.TemporaryDirectory(prefix="tigervnc-jpeg-") as directory:
    copied = Path(directory) / "TigerVNC.app"
    shutil.copytree(app, copied, symlinks=True)
    executable = copied / "Contents/MacOS/jpeg-test"
    shutil.copy2(source, executable)
    libraries = copied / "Contents/Frameworks"
    dependencies = subprocess.check_output(["otool", "-L", str(executable)], text=True)
    for line in dependencies.splitlines()[1:]:
        dependency = line.strip().split(" (", 1)[0]
        if dependency.startswith(("/System/Library/", "/usr/lib/")):
            continue
        name = Path(dependency).resolve().name
        if not (libraries / name).is_file():
            raise RuntimeError(f"Test dependency missing from app: {dependency}")
        subprocess.run(["install_name_tool", "-change", dependency,
                        "@executable_path/../Frameworks/" + name, str(executable)], check=True)
    subprocess.run(["codesign", "--force", "--sign", "-", str(executable)], check=True)
    subprocess.run([str(executable), "--gtest_filter=Jpeg.*"], check=True, timeout=30)
