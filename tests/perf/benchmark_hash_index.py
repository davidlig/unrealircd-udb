#!/usr/bin/env python3
"""Reproducible offline measurement for UDB root-profile hash distributions.

It intentionally models only the UDB djb2/casefold root index. Runtime startup,
mutation, staged sync, RSS and fsync timings require the UnrealIRCd integration
harness and must be collected separately on target hardware.
"""

import argparse
import json
import pathlib
import statistics
import time

MIN_BUCKETS = 2048
LOAD_DENOMINATOR = 4


def hash_key(value: str) -> int:
    result = 5381
    for byte in value.encode("ascii"):
        if 65 <= byte <= 90:
            byte += 32
        result = ((result << 5) + result + byte) & 0xFFFFFFFF
    return result


def bucket_count(entries: int) -> int:
    buckets = MIN_BUCKETS
    while entries > buckets - buckets // LOAD_DENOMINATOR:
        buckets *= 2
    return buckets


def roots(snapshot: pathlib.Path) -> list[str]:
    result: list[str] = []
    seen: set[str] = set()
    for line in snapshot.read_text(encoding="ascii").splitlines():
        if not line or line.startswith(";"):
            continue
        path = line.split(" ", 1)[0]
        root = path.split("::", 1)[0]
        normalized = root.lower()
        if normalized not in seen:
            seen.add(normalized)
            result.append(root)
    return result


def measure(keys: list[str], buckets: int) -> dict[str, float | int]:
    chains = [0] * buckets
    start = time.perf_counter_ns()
    for key in keys:
        chains[hash_key(key) & (buckets - 1)] += 1
    elapsed_ns = time.perf_counter_ns() - start
    nonempty = sorted(value for value in chains if value)
    # A successful chained lookup visits 1..chain_length nodes depending on
    # where the profile falls in the bucket.  This is deterministic and avoids
    # timing noise while still exposing long-chain regressions.
    probes = sorted(probe for value in chains for probe in range(1, value + 1))
    return {
        "profiles_indexed": len(keys),
        "hash_bucket_count": buckets,
        "hash_nonempty_buckets": len(nonempty),
        "hash_max_chain": max(nonempty, default=0),
        "hash_avg_chain_nonempty": statistics.mean(nonempty) if nonempty else 0,
        "hash_probe_avg": statistics.mean(probes) if probes else 0,
        "hash_probe_p95": probes[(len(probes) * 95 + 99) // 100 - 1] if probes else 0,
        "hash_probe_max": max(probes, default=0),
        "index_build_ms": elapsed_ns / 1_000_000,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("snapshot", type=pathlib.Path)
    parser.add_argument("--output", type=pathlib.Path)
    args = parser.parse_args()
    start = time.perf_counter_ns()
    keys = roots(args.snapshot)
    read_ms = (time.perf_counter_ns() - start) / 1_000_000
    report = {
        "dataset": str(args.snapshot),
        "offline_root_parse_ms": read_ms,
        "fixed_2048": measure(keys, MIN_BUCKETS),
        "dynamic": measure(keys, bucket_count(len(keys))),
        "note": "Offline root-index baseline; not an UnrealIRCd runtime or fsync measurement.",
    }
    serialized = json.dumps(report, indent=2, sort_keys=True) + "\n"
    if args.output:
        args.output.write_text(serialized, encoding="utf-8")
    else:
        print(serialized, end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
