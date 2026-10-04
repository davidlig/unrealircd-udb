#!/usr/bin/env python3
"""Explicit local bootstrap of a pinned test dependency (network only on invocation)."""

import argparse
import hashlib
from pathlib import Path
import shutil
import subprocess
import tarfile
import urllib.request

ROOT = Path(__file__).resolve().parents[2]
VERSION = "2.0.2"
SHA256 = "39f92f366bdf3f1a02af4da75b4a5c52df6c9f7e736c7d65de13283f9f0ef416"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cmake", default=shutil.which("cmake"))
    args = parser.parse_args()
    if not args.cmake:
        parser.error("cmake is required; install cmake==3.31.6 in your development environment")
    cache = ROOT / "tests/.build"
    cache.mkdir(parents=True, exist_ok=True)
    archive = cache / f"cmocka-{VERSION}.tar.xz"
    if not archive.is_file():
        with urllib.request.urlopen(f"https://cmocka.org/files/2.0/{archive.name}", timeout=30) as response:
            archive.write_bytes(response.read())
    if hashlib.sha256(archive.read_bytes()).hexdigest() != SHA256:
        parser.error("cmocka archive checksum mismatch; refusing to extract or build")
    with tarfile.open(archive) as package:
        package.extractall(cache, filter="data")
    prefix = cache / "cmocka"
    commands = [
        [args.cmake, "-S", str(cache / f"cmocka-{VERSION}"), "-B", str(cache / "cmocka-build"),
         f"-DCMAKE_INSTALL_PREFIX={prefix}", "-DWITH_EXAMPLES=OFF", "-DUNIT_TESTING=OFF", "-DCMAKE_BUILD_TYPE=Debug"],
        [args.cmake, "--build", str(cache / "cmocka-build")],
        [args.cmake, "--install", str(cache / "cmocka-build")],
    ]
    for command in commands:
        subprocess.run(command, check=True, timeout=180)


if __name__ == "__main__":
    main()
