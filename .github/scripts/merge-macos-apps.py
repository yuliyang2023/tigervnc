#!/usr/bin/env python3
"""Merge two self-contained native app bundles into a Universal 2 app."""

import plistlib
import re
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


def verify_minimum_os(binary, arch, maximum=(13, 0)):
    commands = subprocess.check_output(
        ["otool", "-arch", arch, "-l", str(binary)], text=True
    )
    versions = []
    for block in commands.split("Load command"):
        if re.search(r"cmd LC_BUILD_VERSION\b", block):
            match = re.search(r"\bminos ([\d.]+)", block)
        elif re.search(r"cmd LC_VERSION_MIN_MACOSX\b", block):
            match = re.search(r"\bversion ([\d.]+)", block)
        else:
            continue
        if match is None:
            raise RuntimeError(f"Missing minimum OS version: {binary} ({arch})")
        version = tuple(map(int, match[1].split(".")))
        padded = version + (0,) * (3 - len(version))
        if padded > maximum + (0,):
            raise RuntimeError(f"{binary} ({arch}) requires macOS {match[1]}")
        versions.append(match[1])
    if not versions:
        raise RuntimeError(f"No macOS deployment target found: {binary} ({arch})")
    print(f"{binary.name} ({arch}): minimum macOS {', '.join(versions)}")


def main():
    intel, arm, output = map(Path, sys.argv[1:])
    for app in (intel, arm):
        with (app / "Contents/Info.plist").open("rb") as stream:
            if plistlib.load(stream).get("LSMinimumSystemVersion") != "13.0":
                raise RuntimeError(f"{app}: expected macOS 13.0 in Info.plist")
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
        for arch in ("x86_64", "arm64"):
            verify_minimum_os(target, arch)
        dependencies = subprocess.check_output(
            ["otool", "-L", str(target)], text=True
        )
        for line in dependencies.splitlines():
            if not line.startswith("\t"):
                continue
            dependency = line.strip().split(" (", 1)[0]
            if dependency.startswith("/") and not dependency.startswith(
                ("/usr/lib/", "/System/Library/")
            ):
                raise RuntimeError(f"Unbundled dependency in {relative}: {dependency}")
        run("codesign", "--force", "--sign", "-", str(target))
    run("codesign", "--force", "--deep", "--sign", "-", str(output))
    run("codesign", "--verify", "--deep", "--strict", str(output))
    print(f"Merged and verified {len(arm_files)} Universal Mach-O files")


if __name__ == "__main__":
    main()
