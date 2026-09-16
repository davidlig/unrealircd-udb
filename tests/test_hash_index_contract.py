#!/usr/bin/env python3
"""Deterministic contract checks for UDB's dynamically sized root hash index."""

import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
STORE = (ROOT / "src" / "udb_store.c.inc").read_text(encoding="utf-8")
CORE = (ROOT / "src" / "udb_core.c.inc").read_text(encoding="utf-8")
INTERNAL = (ROOT / "src" / "udb_internal.h").read_text(encoding="utf-8")
MUTATION = (ROOT / "src" / "udb_mutation.c.inc").read_text(encoding="utf-8")
SYNC = (ROOT / "src" / "udb_sync.c.inc").read_text(encoding="utf-8")


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
    for label, source in (("mutation", MUTATION), ("staged commit", SYNC)):
        for match in re.finditer(r"udb_file_write_snapshot\(", source):
            window = source[max(0, match.start() - 800) : match.start()]
            if "udb_hash_prepare_tree(" not in window:
                failures.append(f"{label} does not prepare the index before snapshot commit")
                break
    if "UDB_HASH_SIZE" in INTERNAL + STORE or "UDB_HASH_MASK" in INTERNAL + STORE:
        failures.append("legacy fixed hash constants remain")
    index_fn = re.search(r"static int udb_block_letter_to_index\(char letter\).*?\n}", CORE + STORE, re.S)
    if not index_fn or "return -1;" not in index_fn.group(0):
        failures.append("unknown block letters must not alias a valid block index")
    if failures:
        print("FAIL: " + "\nFAIL: ".join(failures))
        return 1
    print("PASS: dynamic root-index sizing and publication contracts hold")
    return 0


if __name__ == "__main__":
    sys.exit(main())
