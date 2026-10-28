# Benchmarks

Main table and cache/thread scaling table measured 2026-10-28 with
`benchmarks/run_benchmarks.sh` and `stringart-benchmark` (Release build) on
the current code.

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
| 512 | 200 | 2914 | 32 | precomputed 90 MiB | 0.47 | 0.160 | 49 % of 32 | 153 | 0.23822 | 0.03736 | no candidate improves the error |
| 512 | 400 | 3091 | 32 | precomputed 359 MiB | 0.60 | 0.193 | 57 % of 32 | 430 | 0.23821 | 0.03337 | no candidate improves the error |
| 512 | 800 | 3108 | 32 | on demand 0 MiB | 1.26 | 0.405 | 64 % of 32 | 42 | 0.23821 | 0.03145 | no candidate improves the error |
| 1024 | 200 | 4000 | 32 | precomputed 180 MiB | 0.87 | 0.218 | 51 % of 32 | 283 | 0.23819 | 0.05136 | maximum segment count reached |
| 1024 | 400 | 4000 | 32 | on demand 0 MiB | 1.68 | 0.419 | 65 % of 32 | 57 | 0.23818 | 0.04731 | maximum segment count reached |
| 1024 | 800 | 4000 | 32 | on demand 0 MiB | 2.99 | 0.747 | 74 % of 32 | 57 | 0.23818 | 0.04590 | maximum segment count reached |

Cache and thread scaling, 512 px / 400 pins (same 3091-segment result in every row):

| threads | line cache | runtime (s) | peak RAM (MiB) |
|---|---|---|---|
| 1 | on demand | 4.72 | 42 |
| 1 | precomputed 359 MiB | 2.83 | 401 |
| 8 | precomputed 359 MiB | 0.56 | 400 |
| 32 | precomputed 359 MiB | 0.60 | 426 |
| 32 | on demand | 0.68 | 42 |

Observations:

- Single-threaded, the line cache is 1.7× faster than rasterizing on demand.
  With all threads the difference shrinks to ~12 %; the scoring loop becomes
  memory-bound and per-iteration synchronisation (one `blockingMap` per
  segment) dominates beyond ~8 threads.
- Peak RAM with a precomputed cache is the cache plus 40–105 MiB: each pin's
  row of lines stays in its own buffer. (Rows used to be copied into one
  buffer, which needed about twice the cache size, e.g. 771 MiB instead of
  430 MiB at 512 px / 400 pins.)
- At 1024 px, 4000 segments of a 1 px thread do not reach the no-improvement
  point; more segments or a wider thread are needed for comparable density.

Multi-colour, layered (CLI, 512 px, 240 pins, max 4000 steps, same
portrait, defaults: Auto order, 2 refinement passes, 2026-10-27):

| palette | steps | runtime (s) | final error |
|---|---|---|---|
| black (one layer: no refinement, one order) | 3069 | 1.04 | 0.03537 |
| black, brown, beige on cream | 3669 | 5.78 | 0.03323 |

With several colours Auto builds two layer orders and each refinement pass
rebuilds every layer, so the runtime is several single-colour runs. More
results in ARCHITECTURE.md (*Multi-colour threads*).

Layer refinement (Algorithm B) is measured in ARCHITECTURE.md.
