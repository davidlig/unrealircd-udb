"""Official release selection and bounded downloads, without network access."""

import hashlib
import io

import pytest

from udb_test_support.modules import repository_module

pytestmark = pytest.mark.tooling


@pytest.fixture
def resolver():
    return repository_module("scripts/resolve_unrealircd_release.py")


def manifest(version="6.2.7", kind="Stable", url=None):
    return {"6.0": {"Stable": {"type": kind, "version": version, "downloads": {
        "src": url or f"https://www.unrealircd.org/downloads/unrealircd-{version}.tar.gz"}}}}


@pytest.mark.parametrize("version", ["6.2.0", "6.2.7", "6.2.123"])
def test_only_supported_versioned_official_stable_tarballs_are_selected(resolver, version):
    assert resolver.select_release(manifest(version)) == (
        version, f"https://www.unrealircd.org/downloads/unrealircd-{version}.tar.gz")


@pytest.mark.parametrize("value", [None, {}, {"6.0": {}}, {"6.0": {"Stable": {}}},
    manifest("6.3.0"), manifest("6.2.7-dev"), manifest("6.2.7\n"), manifest("6.1.9"),
    manifest(kind="Development"), manifest(url="https://example.org/unrealircd-6.2.7.tar.gz"),
    manifest(url="http://www.unrealircd.org/downloads/unrealircd-6.2.7.tar.gz"),
    manifest(url="https://www.unrealircd.org/downloads/unrealircd-6.2.7.tar.gz?mirror=evil"),
])
def test_invalid_or_untrusted_release_descriptors_fail_closed(resolver, value):
    with pytest.raises(ValueError):
        resolver.select_release(value)


@pytest.mark.parametrize("value,reason", [
    (manifest("6.3.0"), "unsupported"),
    (manifest(kind="Development"), "not Stable"),
    (manifest(url="https://example.org/unrealircd-6.2.7.tar.gz"), "source URL"),
    ({"6.0": {"Stable": {"version": "6.2.7"}}}, "missing"),
])
def test_rejection_identifies_the_manifest_contract_violation(resolver, value, reason):
    with pytest.raises(ValueError, match=reason):
        resolver.select_release(value)


class Response(io.BytesIO):
    def __init__(self, body, url="https://www.unrealircd.org/downloads/archive.tar.gz"):
        super().__init__(body)
        self.url = url

    def geturl(self):
        return self.url


def test_download_hash_matches_exact_saved_bytes(resolver, monkeypatch, tmp_path):
    body = b"bounded archive\x00payload"
    monkeypatch.setattr(resolver.urllib.request, "urlopen", lambda *args, **kwargs: Response(body))
    target = tmp_path / "archive.tar.gz"
    assert resolver.download_archive("https://www.unrealircd.org/a", target) == hashlib.sha256(body).hexdigest()
    assert target.read_bytes() == body


@pytest.mark.parametrize("fault", ["empty", "oversized", "http_redirect"])
def test_download_failure_is_not_silently_accepted(resolver, monkeypatch, tmp_path, fault):
    resolver.MAX_ARCHIVE_BYTES = 8
    body = b"" if fault == "empty" else b"x" * 9 if fault == "oversized" else b"valid"
    url = "http://example.org/a" if fault == "http_redirect" else "https://example.org/a"
    monkeypatch.setattr(resolver.urllib.request, "urlopen", lambda *args, **kwargs: Response(body, url))
    with pytest.raises(ValueError):
        resolver.download_archive("https://www.unrealircd.org/a", tmp_path / "archive")


def test_manifest_download_is_bounded(resolver, monkeypatch):
    resolver.MAX_MANIFEST_BYTES = 8
    monkeypatch.setattr(resolver.urllib.request, "urlopen", lambda *args, **kwargs: Response(b"x" * 9))
    with pytest.raises(ValueError, match="too large"):
        resolver.read_manifest()
