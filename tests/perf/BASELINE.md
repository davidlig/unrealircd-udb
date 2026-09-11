# UDB scalability baseline procedure

This directory provides deterministic, offline root-index measurements. It is
not a substitute for the UnrealIRCd runtime harness: record `startup_ms`, RSS,
snapshot/fdatasync time, mutation time and staged-sync time on the target
machine separately. The generator deliberately does **not** use `; Records:`
for any sizing or verification.

## Reproduce the root-index baseline

```sh
for n in 10000 50000 100000 500000; do
  python3 tests/perf/generate_udb_dataset.py --block N --profiles "$n" \
    --output "tests/perf/out/N-$n.db"
  python3 tests/perf/benchmark_hash_index.py "tests/perf/out/N-$n.db" \
    --output "tests/perf/out/N-$n.json"
done
```

The JSON records the fixed-2048 and dynamically sized index distributions,
including bucket occupancy and probe-count proxies. Run each configuration at
least three times and report medians/p95 with the machine, compiler and module
revision. Keep generated `out/` files out of Git.

## Captured offline baseline (2026-09-11)

One deterministic pass of the minimal-profile dataset on the development
machine produced the following chain/probe distribution. Timings are omitted
from this table because they are Python/offline timings, not UDB event-loop
measurements.

| Profiles | Fixed 2048 max / hit-p95 probes | Dynamic buckets | Dynamic max / hit-p95 probes |
| ---: | ---: | ---: | ---: |
| 10,000 | 14 / 9 | 16,384 | 5 / 3 |
| 50,000 | 38 / 27 | 131,072 | 3 / 2 |
| 100,000 | 70 / 53 | 262,144 | 3 / 2 |
| 500,000 | 306 / 245 | 1,048,576 | 4 / 2 |

The result justifies the index-sizing change, but not a WAL/journal redesign:
no runtime snapshot, fsync, copy-on-write, RSS or staged-sync bottleneck has
yet been measured on representative deployment hardware.

## Runtime collection

Load each generated snapshot through the normal UDB runtime harness, after
copying a freshly built module. Capture `startup_ms`, `peak_rss_kib`,
`block_load_ms`, snapshot and mutation latency, and `staged_sync_ms` externally
(the module deliberately has no permanently enabled detailed instrumentation).
Use the same dataset, machine, configuration and iteration count before and
after a hash-index change.
