#!/usr/bin/env python3
"""Measure the real native engine in owned scratch stores, never installed data.

This is a storage-core bulk build/recovery/indexed read benchmark, NOT a daemon
throughput benchmark or a power-loss durability test. The OS cache is not flushed.
"""
import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile
import time
from udb_test_support.build import build_storage_driver

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--rows', type=int, choices=[10000, 1000000, 5000000, 10000000], required=True)
    parser.add_argument('--cache-mib', type=int, choices=[128, 256, 512], required=True)
    parser.add_argument('--point-writes', action='store_true', help='Also measure ten durable point mutations plus origin binding after reads.')
    parser.add_argument('--timeout', type=int, default=120)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if not 1 <= args.timeout <= 900:
        parser.error('timeout must be 1..900 seconds')
    build = ROOT / 'tests/.build'
    build.mkdir(exist_ok=True)
    started = time.time()
    with tempfile.TemporaryDirectory(prefix='native-benchmark-', dir=build) as scratch:
        scratch = Path(scratch)
        binary = build_storage_driver(scratch / ('driver.exe' if os.name == 'nt' else 'driver'))
        store = scratch / 'store'
        store.mkdir()
        result = subprocess.run([str(binary), 'benchmark', str(store), str(args.rows), str(args.cache_mib * 1048576)] + (['points'] if args.point_writes else []),
                                capture_output=True, text=True, timeout=args.timeout)
        if result.returncode:
            raise RuntimeError(f'benchmark exited {result.returncode}: {result.stdout}{result.stderr}')
        report = json.loads(result.stdout)
        report.update(timestamp_unix=started, note='Storage core; OS cache retained; no IRC throughput or power-loss claim.')
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(report, indent=2) + '\n')
        print(json.dumps(report))


if __name__ == '__main__':
    main()
