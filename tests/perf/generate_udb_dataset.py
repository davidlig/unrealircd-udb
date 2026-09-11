#!/usr/bin/env python3
"""Generate deterministic UDB snapshots for scalability benchmarks.

The generator is deliberately outside production code.  Its Records header is
informational only; consumers must parse and validate the actual records.
"""

import argparse
import pathlib


def emit(path: pathlib.Path, block: str, profiles: int, seed: int) -> None:
    lines = [f"; UDB benchmark dataset: block={block} profiles={profiles} seed={seed}", "; Generation: 1"]
    for number in range(profiles):
        key = f"bench{seed:08x}-{number:08x}"
        if block == "N":
            lines.extend((
                f"{key}::pass sha256:{number:064x}",
                f"{key}::vhost bench-{number:08x}.example.test",
                f"{key}::modes +i",
            ))
        elif block == "C":
            channel = f"#bench-{seed:08x}-{number:08x}"
            lines.extend((
                f"{channel}::modes +nt",
                f"{channel}::topic deterministic benchmark topic {number}",
                f"{channel}::founder founder{number:08x}",
            ))
        elif block == "K":
            lines.extend((
                f"G::{key}.example.test::reason benchmark-{number:08x}",
            ))
        elif block == "I":
            octet2, octet3, octet4 = (number >> 16) & 0xff, (number >> 8) & 0xff, number & 0xff
            lines.append(f"10.{octet2}.{octet3}.{octet4}::clones *1")
        else:
            raise ValueError(f"large-profile benchmark is not valid for block {block}")
    logical_records = len(lines) - 2
    lines.insert(1, f"; Records: {logical_records}")
    path.write_text("\n".join(lines) + "\n", encoding="ascii")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--block", choices=("N", "C", "I", "K"), default="N")
    parser.add_argument("--profiles", type=int, required=True)
    parser.add_argument("--seed", type=lambda value: int(value, 0), default=0x554442)
    parser.add_argument("--output", type=pathlib.Path, required=True)
    args = parser.parse_args()
    if args.profiles < 0:
        parser.error("--profiles must be non-negative")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    emit(args.output, args.block, args.profiles, args.seed)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
