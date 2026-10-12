# Native storage benchmark reference

The maintained storage benchmark is [benchmark_native.py](benchmark_native.py).
See the [test guide](../README.md#exclusive-native-persistence-qualification) for
setup, supported dataset/cache sizes, commands and output locations.

The benchmark builds the production storage engine and measures bulk build,
recovery and indexed reads using owned scratch stores. Optional point-write
measurements exercise durable mutations. It never writes installed server data.

OS caches are retained: these measurements do not demonstrate cold-device
performance, IRC throughput, runtime convergence or power-loss durability.
Record the machine, compiler, source revision and cache size with each report.
Keep generated stores and reports outside tracked source files.

Observed Linux results and their limitations are documented in the
[technical qualification](../../doc/udb_technical_en.md#21-linux-qualification-evidence)
and its [machine-readable report](../../doc/native_storage_benchmarks.json).
