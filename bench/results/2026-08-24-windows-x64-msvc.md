# DEUS benchmark baseline — 2026-08-24

- commit under test: `b74cc1e` plus the benchmark working tree;
- benchmark protocol: 1;
- build: Release, MSVC 19.44.35228, Ninja;
- platform: Windows x64;
- workload: 200 scalar bindings, 4,103 source bytes, 402 VM instructions;
- samples: 3 consecutive runs from the same local workspace.

| Metric | Samples | Median |
| --- | --- | --- |
| Compile | 209.207, 502.627, 218.267 µs/iteration | 218.267 µs |
| Bytecode write/read | 1,338.827, 1,263.516, 1,102.802 µs/iteration | 1,263.516 µs |
| VM execution | 6.057, 5.886, 6.206 µs/iteration | 6.057 µs |
| VM throughput | 66,367,298, 68,296,495, 64,770,804 instructions/s | 66,367,298 instructions/s |
| Peak working set | 5.629, 6.828, 5.617 MiB | 5.629 MiB |

The second compile sample is an observed outlier and remains in the record.
Bytecode results include local filesystem I/O. This baseline is suitable for
regression comparison on equivalent hardware, not for cross-language claims.
