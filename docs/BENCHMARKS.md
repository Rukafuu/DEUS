# Benchmarks

DEUS benchmarks exist to detect regressions, not to claim superiority over
unrelated languages. They are excluded from normal builds and exercise public
compiler, bytecode and VM APIs using a deterministic 200-binding scalar program.

## Build and run

Configure a dedicated optimized build from a Visual Studio developer prompt:

```powershell
cmake -S . -B build-bench -G Ninja -DCMAKE_BUILD_TYPE=Release -DDEUS_BUILD_BENCHMARKS=ON
cmake --build build-bench --parallel
.\build-bench\deus-bench.exe
```

The output reports compilation latency, bytecode write/read latency, scalar VM
throughput and peak process working set. Bytecode measurements include local
filesystem I/O and therefore must only be compared on equivalent storage.

## Recording results

Keep the complete output together with the commit, build type, compiler,
operating system, CPU and storage context. Compare repeated runs from an idle
machine and use medians; a single run is only a smoke measurement. Do not treat
peak working set as precise allocator accounting.

The `DEUS benchmark protocol 1` header identifies the current workload and
output contract. Change the protocol number whenever workloads or metric
definitions change so historical results are not compared silently.
