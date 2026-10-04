"""Build test binaries only; never install/rebuild the production module here."""

import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[3]


def build_c_suite(profile: str, environment=None) -> Path:
    if profile not in {"normal", "asan"}:
        raise ValueError("unknown C test profile")
    env = dict(os.environ if environment is None else environment)
    source = Path(env.get("UNREALIRCD_SOURCE", str(ROOT.parents[3])))
    prefix = Path(env.get("CMOCKA_PREFIX", str(ROOT / "tests/.build/cmocka")))
    missing = [str(path) for path in (source / "include/config.h", source / "include/unrealircd.h",
                                     prefix / "include/cmocka.h") if not path.is_file()]
    if missing:
        raise RuntimeError("C suite prerequisites unavailable: " + ", ".join(missing)
                           + "; configure UnrealIRCd and run tests/support/bootstrap_cmocka.py")
    result = subprocess.run(
        ["make", "-C", str(ROOT / "tests"), f"PROFILE={profile}",
         f"UNREALIRCD_SOURCE={source}", f"CMOCKA_PREFIX={prefix}"],
        env=env, capture_output=True, text=True, timeout=180,
    )
    if result.returncode:
        raise RuntimeError("C suite build failed:\n" + result.stdout + result.stderr)
    return ROOT / "tests/.build" / profile
