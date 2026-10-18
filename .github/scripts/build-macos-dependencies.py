#!/usr/bin/env python3
"""Build pinned runtime dependencies for macOS 13 on the CI runner."""

import hashlib
import json
import os
import subprocess
import sys
from pathlib import Path


PREFIX = Path(sys.argv[1]).resolve()
ARCH = sys.argv[2]
ROOT = Path(os.environ["RUNNER_TEMP"]) / "tigervnc-dependencies"
LOCK = json.loads(Path(__file__).with_name("macos-dependencies.json").read_text())
ENV = os.environ.copy()
ENV.update({
    "MACOSX_DEPLOYMENT_TARGET": "13.0",
    "CC": "clang", "CXX": "clang++",
    "CFLAGS": f"-O2 -arch {ARCH} -mmacosx-version-min=13.0",
    "CXXFLAGS": f"-O2 -arch {ARCH} -mmacosx-version-min=13.0",
    "CPPFLAGS": f"-I{PREFIX}/include",
    "LDFLAGS": f"-arch {ARCH} -mmacosx-version-min=13.0 -L{PREFIX}/lib",
    "PKG_CONFIG_PATH": f"{PREFIX}/lib/pkgconfig",
    "PKG_CONFIG_LIBDIR": f"{PREFIX}/lib/pkgconfig",
    "PATH": f"{PREFIX}/bin:{ENV['PATH']}",
})


def run(*args, cwd=None):
    subprocess.run(list(map(str, args)), cwd=cwd, env=ENV, check=True)


def download(url, destination, checksum):
    # Use the canonical GNU mirror rather than a geographically selected mirror.
    url = url.replace("https://ftpmirror.gnu.org/", "https://ftp.gnu.org/gnu/")
    run("curl", "--fail", "--location", "--retry", "4", "--max-time", "300",
        url, "--output", destination)
    if hashlib.sha256(destination.read_bytes()).hexdigest() != checksum:
        raise RuntimeError(f"Checksum mismatch: {url}")


def source(name):
    metadata = LOCK[name]
    dest = ROOT / name
    dest.mkdir(parents=True)
    archive = ROOT / (name + ".tar")
    download(metadata["url"], archive, metadata["checksum"])
    run("tar", "-xf", archive, "--strip-components=1", "-C", dest)
    for index, patch in enumerate(metadata["patches"]):
        patchfile = ROOT / f"{name}-{index}.patch"
        download(patch["url"], patchfile, patch["sha256"])
        run("patch", "-" + patch["strip"], "-i", patchfile, cwd=dest)
    return dest


def autotools(name, *options, subdir=""):
    print(f"::group::Build {name} for macOS 13", flush=True)
    directory = source(name) / subdir
    run("./configure", f"--prefix={PREFIX}", "--enable-shared", "--disable-static",
        "--disable-dependency-tracking", *options, cwd=directory)
    run("make", "-j3", cwd=directory)
    run("make", "install", cwd=directory)
    print("::endgroup::", flush=True)


def cmake(name, *options):
    print(f"::group::Build {name} for macOS 13", flush=True)
    directory = source(name)
    run("cmake", "-S", directory, "-B", directory / "build",
        f"-DCMAKE_INSTALL_PREFIX={PREFIX}", "-DCMAKE_BUILD_TYPE=Release",
        "-DCMAKE_POLICY_VERSION_MINIMUM=3.5",
        "-DCMAKE_OSX_DEPLOYMENT_TARGET=13.0", f"-DCMAKE_OSX_ARCHITECTURES={ARCH}",
        f"-DCMAKE_PREFIX_PATH={PREFIX}", *options)
    run("cmake", "--build", directory / "build", "--parallel", "3")
    run("cmake", "--install", directory / "build")
    print("::endgroup::", flush=True)


PREFIX.mkdir(parents=True, exist_ok=True)
ROOT.mkdir(parents=True, exist_ok=True)
autotools("gettext", "--disable-java", "--disable-c++", "--disable-libasprintf",
          "--without-libiconv-prefix", subdir="gettext-runtime")
autotools("gmp", "--enable-cxx")
autotools("nettle", "--disable-documentation")
autotools("libtasn1", "--disable-doc")
autotools("libunistring", "--without-libiconv-prefix")
autotools("gnutls", "--disable-doc", "--disable-tools", "--disable-tests",
          "--disable-cxx", "--disable-libdane", "--without-p11-kit", "--without-idn",
          "--without-tpm", "--without-tpm2", "--without-brotli", "--without-zstd",
          "--without-zlib")
cmake("jpeg-turbo", "-DENABLE_SHARED=ON", "-DENABLE_STATIC=OFF")
cmake("fltk@1.3", "-DOPTION_BUILD_SHARED_LIBS=ON", "-DFLTK_BUILD_TEST=OFF",
      "-DFLTK_BUILD_EXAMPLES=OFF", "-DOPTION_USE_GL=OFF",
      "-DOPTION_USE_SYSTEM_LIBPNG=OFF", "-DOPTION_USE_SYSTEM_LIBJPEG=OFF")
print("::group::Build pixman for macOS 13", flush=True)
pixman = source("pixman")
run("meson", "setup", pixman / "build", pixman, f"--prefix={PREFIX}",
    "--libdir=lib", "--buildtype=release", "--default-library=shared",
    "-Dtests=disabled", "-Ddemos=disabled", "-Dgtk=disabled", "-Dlibpng=disabled")
run("meson", "compile", "-C", pixman / "build", "-j3")
run("meson", "install", "-C", pixman / "build")
print("::endgroup::", flush=True)
cmake("googletest", "-DBUILD_SHARED_LIBS=OFF", "-DCMAKE_CXX_STANDARD=17")
