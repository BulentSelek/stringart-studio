# Benchmarks

Measured 2026-10-26 with `benchmarks/run_benchmarks.sh` (Release build), re-run
after Phase 2 (the mask changed from the exact circle to the pins' convex
hull, which shifts errors in the 5th decimal). The cache/thread scaling table
below was measured on the Phase 1 code.

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
| 512 | 200 | 2914 | 32 | precomputed 90 MiB | 0.52 | 0.177 | 42 % of 32 | 243 | 0.23822 | 0.03736 | no candidate improves the error |
| 512 | 400 | 3091 | 32 | precomputed 359 MiB | 0.68 | 0.220 | 47 % of 32 | 767 | 0.23821 | 0.03337 | no candidate improves the error |
| 512 | 800 | 3108 | 32 | on demand 0 MiB | 1.20 | 0.385 | 68 % of 32 | 45 | 0.23821 | 0.03145 | no candidate improves the error |
| 1024 | 200 | 4000 | 32 | precomputed 180 MiB | 0.94 | 0.234 | 46 % of 32 | 457 | 0.23819 | 0.05136 | maximum segment count reached |
| 1024 | 400 | 4000 | 32 | on demand 0 MiB | 1.70 | 0.424 | 63 % of 32 | 57 | 0.23818 | 0.04731 | maximum segment count reached |
| 1024 | 800 | 4000 | 32 | on demand 0 MiB | 2.97 | 0.742 | 74 % of 32 | 57 | 0.23818 | 0.04590 | maximum segment count reached |

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
