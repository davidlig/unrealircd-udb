#!/usr/bin/env python3
"""Resolve one supported UnrealIRCd Stable tarball for a CI run."""

import argparse
import hashlib
import json
import re
import urllib.parse
import urllib.request
from pathlib import Path

MANIFEST_URL = "https://www.unrealircd.org/downloads/list.json"
MAX_MANIFEST_BYTES = 1024 * 1024
MAX_ARCHIVE_BYTES = 100 * 1024 * 1024
REQUEST_HEADERS = {"User-Agent": "UDB-CI/1.0"}


def select_release(manifest):
    try:
        release = manifest["6.0"]["Stable"]
        version = release["version"]
        source_url = release["downloads"]["src"]
    except (KeyError, TypeError) as exc:
        raise ValueError("missing UnrealIRCd 6.0 Stable release fields") from exc

    if release.get("type") != "Stable":
        raise ValueError("the selected release is not Stable")
    if not isinstance(version, str) or not re.fullmatch(r"6\.2\.[0-9]+", version):
        raise ValueError(f"unsupported UnrealIRCd Stable version: {version!r}")
    expected_url = f"https://www.unrealircd.org/downloads/unrealircd-{version}.tar.gz"
    if source_url != expected_url:
        raise ValueError("Stable source URL does not match the official versioned tarball")
    return version, source_url


def read_manifest():
    request = urllib.request.Request(MANIFEST_URL, headers=REQUEST_HEADERS)
    with urllib.request.urlopen(request, timeout=30) as response:
        body = response.read(MAX_MANIFEST_BYTES + 1)
    if len(body) > MAX_MANIFEST_BYTES:
        raise ValueError("UnrealIRCd release manifest is too large")
    return json.loads(body)


def download_archive(url, destination):
    digest = hashlib.sha256()
    total = 0
    request = urllib.request.Request(url, headers=REQUEST_HEADERS)
    with urllib.request.urlopen(request, timeout=60) as response:
        if urllib.parse.urlsplit(response.geturl()).scheme != "https":
            raise ValueError("UnrealIRCd archive redirect is not HTTPS")
        with destination.open("wb") as output:
            while chunk := response.read(1024 * 1024):
                total += len(chunk)
                if total > MAX_ARCHIVE_BYTES:
                    raise ValueError("UnrealIRCd archive is too large")
                output.write(chunk)
                digest.update(chunk)
    if not total:
        raise ValueError("UnrealIRCd archive is empty")
    return digest.hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--archive", required=True, type=Path)
    parser.add_argument("--github-output", required=True, type=Path)
    args = parser.parse_args()

    version, source_url = select_release(read_manifest())
    checksum = download_archive(source_url, args.archive)
    with args.github_output.open("a", encoding="utf-8") as output:
        output.write(f"version={version}\nurl={source_url}\nsha256={checksum}\n")
    print(f"Selected UnrealIRCd {version} ({checksum})")


if __name__ == "__main__":
    main()
