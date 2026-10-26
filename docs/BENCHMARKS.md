# Benchmarks

Measured 2026-10-26 with `benchmarks/run_benchmarks.sh` (Release build).

- CPU: Intel Core i9-13980HX (8 P-cores + 16 E-cores, 32 hardware threads)
- RAM: 32 GB; OS: Arch Linux, kernel 7.2; GCC 16.2.1; Qt 6.12.0
- Input: synthetic portrait (`examples/SyntheticImages.h`), circular pins,
  min. separation = pins / 10, opacity 0.2, width 1 px, max 4000 segments,
  line-cache budget 1 GiB.
- Runtime is wall time of `GreedyOptimizer::optimize` only, including line cache
  construction. CPU utilisation = CPU time / wall time / threads.
  Peak RAM = process peak RSS (`getrusage`).
- Error = weighted MSE in sRGB-encoded space (see ARCHITECTURE.md).

| size | pins | segments | threads | line cache | runtime (s) | ms/segment | CPU util. | peak RAM (MiB) | initial error | final error | stop reason |
|---|---|---|---|---|---|---|---|---|---|---|---|
| 512 | 200 | 2906 | 32 | precomputed 90 MiB | 0.48 | 0.166 | 47 % of 32 | 239 | 0.23821 | 0.03737 | no candidate improves the error |
| 512 | 400 | 3091 | 32 | precomputed 359 MiB | 0.70 | 0.228 | 47 % of 32 | 771 | 0.23821 | 0.03337 | no candidate improves the error |
| 512 | 800 | 3108 | 32 | on demand 0 MiB | 1.34 | 0.431 | 62 % of 32 | 45 | 0.23821 | 0.03145 | no candidate improves the error |
| 1024 | 200 | 4000 | 32 | precomputed 180 MiB | 0.99 | 0.246 | 45 % of 32 | 449 | 0.23818 | 0.05139 | maximum segment count reached |
| 1024 | 400 | 4000 | 32 | on demand 0 MiB | 1.84 | 0.459 | 60 % of 32 | 57 | 0.23818 | 0.04731 | maximum segment count reached |
| 1024 | 800 | 4000 | 32 | on demand 0 MiB | 3.31 | 0.827 | 69 % of 32 | 57 | 0.23818 | 0.04590 | maximum segment count reached |

Cache and thread scaling, 512 px / 400 pins (same 3091-segment result in every row):

| threads | line cache | runtime (s) | peak RAM (MiB) |
|---|---|---|---|
| 1 | on demand | 5.07 | 45 |
| 1 | precomputed 359 MiB | 2.69 | 788 |
| 8 | precomputed 359 MiB | 0.75 | 795 |
| 32 | precomputed 359 MiB | 0.70 | 771 |
| 32 | on demand | 0.78 | 45 |

Observations:

- Single-threaded, the line cache is 1.9× faster than rasterizing on demand.
  With all threads the difference shrinks to ~10 %; the scoring loop becomes
  memory-bound and per-iteration synchronisation (one `blockingMap` per
  segment) dominates beyond ~8 threads.
- Peak RAM with a precomputed cache is about 2× the cache size because rows
  are rasterized into temporary vectors before being flattened.
- At 1024 px, 4000 segments of a 1 px thread do not reach the no-improvement
  point; more segments or a wider thread are needed for comparable density.
