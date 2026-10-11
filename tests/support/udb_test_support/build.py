"""Build test binaries only; never install/rebuild the production module here."""

import os
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[3]


def _test_build_root(env):
    return Path(env.get("UDB_TEST_BUILD_ROOT", ROOT / "tests/.build"))


def _run(command, *, env, timeout, label):
    result = subprocess.run(command, env=env, capture_output=True, text=True, timeout=timeout)
    if result.returncode:
        raise RuntimeError(f"{label} failed:\n{result.stdout}{result.stderr}")
    return result


def _windows_test_paths(env):
    source = Path(env["UNREALIRCD_SOURCE"]) if env.get("UNREALIRCD_SOURCE") else ROOT.parents[3]
    libraries = Path(env.get("UDB_TEST_LIBRARIES_ROOT", str(source.parent / "unrealircd-libraries-6.2.7")))
    cmocka = Path(env.get("CMOCKA_PREFIX", str(ROOT / "tests/.build/cmocka")))
    openssl = libraries / "openssl"
    missing = [str(path) for path in (source / "include/config.h", openssl / "include/openssl/sha.h",
                                      openssl / "lib/libcrypto.lib") if not path.is_file()]
    if missing:
        raise RuntimeError("Windows test prerequisites unavailable: " + ", ".join(missing))
    compiler = shutil.which("cl.exe") or shutil.which("cl")
    if not compiler:
        raise RuntimeError("MSVC is not on PATH; run pytest from an x64 Visual Studio 2019 developer prompt")
    return source, libraries, openssl, cmocka, compiler


def build_storage_driver(output: Path, environment=None, profile: str = "normal") -> Path:
    if profile not in {"normal", "asan"}:
        raise ValueError("unknown C test profile")
    env = dict(os.environ if environment is None else environment)
    source = ROOT / "tests/native/storage_driver.c"
    output = Path(output)
    output.parent.mkdir(parents=True, exist_ok=True)

    if os.name == "nt":
        if profile == "asan" or env.get("UDB_NATIVE_ASAN") == "1":
            raise RuntimeError("MSVC v142 cannot provide this runner's combined ASan/UBSan profile")
        _, _, openssl, _, compiler = _windows_test_paths(env)
        obj_dir = output.parent / "obj"
        obj_dir.mkdir(parents=True, exist_ok=True)
        command = [compiler, "/nologo", "/std:c11", "/MD", "/O1", "/Zi", "/W3", "/wd4996",
                   "/D_CRT_SECURE_NO_WARNINGS", "/D_CRT_NONSTDC_NO_WARNINGS",
                   f"/I{openssl / 'include'}", f"/Fo{obj_dir / ''}", f"/Fe{output}", str(source),
                   "/link", f"/LIBPATH:{openssl / 'lib'}", "libcrypto.lib", "advapi32.lib", "psapi.lib"]
    else:
        command = [env.get("CC", "cc"), "-std=gnu17", "-O1", "-g", "-Wno-deprecated-declarations",
                   str(source), "-pthread", "-lcrypto", "-o", str(output)]
        if profile == "asan" or env.get("UDB_NATIVE_ASAN") == "1":
            command[1:1] = ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]

    _run(command, env=env, timeout=180, label="native storage driver build")
    return output


def build_c_suite(profile: str, environment=None) -> Path:
    if profile not in {"normal", "asan"}:
        raise ValueError("unknown C test profile")
    env = dict(os.environ if environment is None else environment)
    source = Path(env["UNREALIRCD_SOURCE"]) if env.get("UNREALIRCD_SOURCE") else ROOT.parents[3]
    prefix = Path(env.get("CMOCKA_PREFIX", str(ROOT / "tests/.build/cmocka")))
    missing = [str(path) for path in (source / "include/config.h", source / "include/unrealircd.h",
                                     prefix / "include/cmocka.h") if not path.is_file()]
    if missing:
        raise RuntimeError("C suite prerequisites unavailable: " + ", ".join(missing)
                           + "; configure UnrealIRCd and run tests/support/bootstrap_cmocka.py")

    if os.name == "nt":
        if profile == "asan":
            raise RuntimeError("MSVC v142 does not support the combined ASan/UBSan C suite profile")
        source, libraries, openssl, cmocka, compiler = _windows_test_paths(env)
        cmocka_header = cmocka / "include/cmocka.h"
        cmocka_lib = cmocka / "lib/cmocka.lib"
        cmocka_dll = cmocka / "bin/cmocka.dll"
        pcre2 = libraries / "pcre2"
        pcre2_lib = pcre2 / "lib/pcre2-8.lib"
        if not cmocka_header.is_file() or not cmocka_lib.is_file() or not cmocka_dll.is_file() or not pcre2_lib.is_file():
            raise RuntimeError("Native C suite dependencies unavailable; bootstrap CMocka and configure the Windows library pack")
        output = _test_build_root(env) / profile
        objects = output / "obj"
        output.mkdir(parents=True, exist_ok=True)
        objects.mkdir(parents=True, exist_ok=True)
        dependency_includes = [f"/I{item / 'include'}" for item in libraries.iterdir()
                               if item.is_dir() and (item / "include").is_dir()]
        flags = [compiler, "/nologo", "/std:c11", "/MD", "/Zi", "/O1", "/Gy", "/Gw", "/J", "/W3",
                 "/wd4996", "/D_CRT_SECURE_NO_WARNINGS", "/D_CRT_NONSTDC_NO_WARNINGS",
                 "/DWIN32_LEAN_AND_MEAN", f"/I{ROOT}", f"/I{ROOT / 'tests/support/c'}",
                 f"/I{source / 'include'}", f"/I{libraries / 'jansson/include'}",
                 f"/I{libraries / 'argon2/include'}", f"/I{libraries / 'libsodium/src/libsodium/include'}",
                 f"/I{openssl / 'include'}",
                 f"/I{pcre2 / 'include'}", f"/I{cmocka / 'include'}", *dependency_includes]
        common_sources = [ROOT / "tests/support/c/host.c", source / "src/match.c", source / "src/support.c"]
        common_objects = []
        for item in common_sources:
            obj = objects / f"{item.stem}-{len(common_objects)}.obj"
            command = flags + [f"/Fo{obj}", "/c", str(item)]
            _run(command, env=env, timeout=120, label=f"MSVC compile {item.name}")
            common_objects.append(obj)
        for item in sorted((ROOT / "tests/unit").glob("test_*.c")):
            test_obj = objects / f"{item.stem}.obj"
            binary = output / f"{item.stem}.exe"
            command = flags + ["/Dsafe_alloc=__wrap_safe_alloc", "/Dour_strdup=__wrap_our_strdup",
                               f"/Fo{test_obj}", "/c", str(item)]
            _run(command, env=env, timeout=180, label=f"MSVC compile {item.name}")
            link = [compiler, "/nologo", f"/Fe{binary}", *(str(obj) for obj in [test_obj, *common_objects]),
                    "/link", "/SUBSYSTEM:CONSOLE", "/OPT:REF", "/OPT:ICF",
                    f"/LIBPATH:{cmocka / 'lib'}", f"/LIBPATH:{openssl / 'lib'}", f"/LIBPATH:{pcre2 / 'lib'}",
                    f"/LIBPATH:{source}", f"/LIBPATH:{libraries / 'libsodium/bin/x64/Release/v142/dynamic'}",
                    "cmocka.lib", "libcrypto.lib", "pcre2-8.lib", "UnrealIRCd.lib", "libsodium.lib",
                    "advapi32.lib", "ws2_32.lib", "psapi.lib"]
            _run(link, env=env, timeout=120, label=f"MSVC link {item.stem}")
        shutil.copy2(cmocka_dll, output / "cmocka.dll")
        return output

    result = subprocess.run(
        ["make", "-C", str(ROOT / "tests"), f"OUT={_test_build_root(env) / profile}", f"PROFILE={profile}",
         f"UNREALIRCD_SOURCE={source}", f"CMOCKA_PREFIX={prefix}"],
        env=env, capture_output=True, text=True, timeout=180,
    )
    if result.returncode:
        raise RuntimeError("C suite build failed:\n" + result.stdout + result.stderr)
    return _test_build_root(env) / profile
