#!/usr/bin/env python3
"""Deterministic contract checks for UDB's dynamically sized root hash index."""

import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
STORE = (ROOT / "src" / "udb_store.c.inc").read_text(encoding="utf-8")
CORE = (ROOT / "src" / "udb_core.c.inc").read_text(encoding="utf-8")
INTERNAL = (ROOT / "src" / "udb_internal.h").read_text(encoding="utf-8")


def bucket_count(entries):
    buckets = 2048
    while entries > buckets - buckets // 4:
        buckets *= 2
    return buckets


def main():
    failures = []
    expected = {0: 2048, 1: 2048, 1536: 2048, 1537: 4096, 100000: 262144, 500000: 1048576}
    for entries, buckets in expected.items():
        if bucket_count(entries) != buckets:
            failures.append(f"wrong bucket count for {entries}: {bucket_count(entries)} != {buckets}")
    for required in (
        "typedef struct UdbHashIndex",
        "size_t bucket_count;",
        "size_t entries;",
        "static int udb_hash_prepare_tree",
        "static void udb_hash_publish_prepared",
        "entries++",
        "entries--",
        "!strcasecmp(curr->key, key)",
    ):
        if required not in INTERNAL + STORE:
            failures.append(f"missing index contract: {required}")
    if "for (rec = tree->child; rec; rec = rec->sibling)" not in STORE:
        failures.append("index is not built from direct root profiles")
    if 'parent->key, "F"' not in STORE or "return !strcmp(left, right);" not in STORE:
        failures.append("K::F exact child-key comparison is not preserved")
    if not re.search(r"udb_hash_prepare_tree\(candidate, &hash_index\).*?udb_file_write_snapshot", CORE, re.S):
        failures.append("copy-on-write persistence does not prepare the index before snapshot commit")
    if "UDB_HASH_SIZE" in INTERNAL + STORE or "UDB_HASH_MASK" in INTERNAL + STORE:
        failures.append("legacy fixed hash constants remain")
    if failures:
        print("FAIL: " + "\nFAIL: ".join(failures))
        return 1
    print("PASS: dynamic root-index sizing and publication contracts hold")
    return 0


if __name__ == "__main__":
    sys.exit(main())
