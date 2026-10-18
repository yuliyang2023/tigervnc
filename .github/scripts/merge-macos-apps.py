#!/usr/bin/env python3
"""Merge two self-contained native app bundles into a Universal 2 app."""

import shutil
import subprocess
import sys
from pathlib import Path


MACHO_MAGIC = {
    b"\xfe\xed\xfa\xce", b"\xce\xfa\xed\xfe",
    b"\xfe\xed\xfa\xcf", b"\xcf\xfa\xed\xfe",
    b"\xca\xfe\xba\xbe", b"\xbe\xba\xfe\xca",
    b"\xca\xfe\xba\xbf", b"\xbf\xba\xfe\xca",
}


def run(*args):
    subprocess.run(args, check=True)


def binaries(app):
    result = set()
    for path in app.rglob("*"):
        if path.is_symlink() or not path.is_file():
            continue
        with path.open("rb") as stream:
            if stream.read(4) in MACHO_MAGIC:
                result.add(path.relative_to(app))
    return result


def main():
    intel, arm, output = map(Path, sys.argv[1:])
    intel_files = binaries(intel)
    arm_files = binaries(arm)
    if intel_files != arm_files:
        raise RuntimeError(
            f"App library sets differ. Intel only: {intel_files - arm_files}; "
            f"ARM only: {arm_files - intel_files}"
        )
    if Path("Contents/MacOS/vncviewer") not in arm_files:
        raise RuntimeError("App does not contain a Mach-O viewer executable")
    shutil.copytree(arm, output, symlinks=True)
    shutil.rmtree(output / "Contents/_CodeSignature", ignore_errors=True)
    for relative in sorted(arm_files):
        target = output / relative
        # lipo cannot overwrite an input file, so use a sibling temporary file.
        merged = target.with_name(target.name + ".universal")
        run("lipo", str(intel / relative), "-verify_arch", "x86_64")
        run("lipo", str(arm / relative), "-verify_arch", "arm64")
        run("lipo", "-create", str(intel / relative), str(arm / relative),
            "-output", str(merged))
        merged.chmod(target.stat().st_mode)
        merged.replace(target)
        run("lipo", str(target), "-verify_arch", "x86_64", "arm64")
        dependencies = subprocess.check_output(
            ["otool", "-L", str(target)], text=True
        )
        if "/opt/homebrew/" in dependencies or "/usr/local/" in dependencies:
            raise RuntimeError(f"Unbundled Homebrew dependency in {relative}")
        run("codesign", "--force", "--sign", "-", str(target))
    run("codesign", "--force", "--deep", "--sign", "-", str(output))
    run("codesign", "--verify", "--deep", "--strict", str(output))
    print(f"Merged and verified {len(arm_files)} Universal Mach-O files")


if __name__ == "__main__":
    main()
