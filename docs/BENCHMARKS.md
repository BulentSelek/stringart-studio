# Benchmarks

All tables measured 2026-10-28 with
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
| 512 | 200 | 2914 | 16 | precomputed 90 MiB | 0.33 | 0.114 | 54 % of 16 | 150 | 0.23822 | 0.03736 | no candidate improves the error |
| 512 | 400 | 3091 | 16 | precomputed 359 MiB | 0.60 | 0.195 | 68 % of 16 | 426 | 0.23821 | 0.03337 | no candidate improves the error |
| 512 | 800 | 3108 | 32 | on demand 0 MiB | 1.32 | 0.424 | 62 % of 32 | 42 | 0.23821 | 0.03145 | no candidate improves the error |
| 1024 | 200 | 4000 | 16 | precomputed 180 MiB | 0.84 | 0.211 | 61 % of 16 | 284 | 0.23819 | 0.05136 | maximum segment count reached |
| 1024 | 400 | 4000 | 32 | on demand 0 MiB | 1.92 | 0.479 | 57 % of 32 | 58 | 0.23818 | 0.04731 | maximum segment count reached |
| 1024 | 800 | 4000 | 32 | on demand 0 MiB | 3.41 | 0.853 | 65 % of 32 | 57 | 0.23818 | 0.04590 | maximum segment count reached |

Cache and thread scaling, 512 px / 400 pins (same 3091-segment result in every row):

| threads | line cache | runtime (s) | peak RAM (MiB) |
|---|---|---|---|
| 1 | on demand | 4.54 | 42 |
| 1 | precomputed 359 MiB | 2.82 | 400 |
| 8 | precomputed 359 MiB | 0.62 | 401 |
| 16 | precomputed 359 MiB | 0.54 | 403 |
| 32 | precomputed 359 MiB | 0.60 | 422 |
| 32 | on demand | 0.70 | 42 |

Observations:

- Single-threaded, the line cache is 1.6× faster than rasterizing on demand.
  With many threads the difference shrinks; scoring from the cache is
  memory-bound and stops scaling at about 16 threads (32 threads: slower,
  with more CPU time). Automatic mode therefore uses at most 16 threads
  per scoring loop when the lines are precomputed, and all cores when they
  are rasterized on demand, which is compute-bound (1024 px / 800 pins:
  3.4 s with 32 threads, 4.2 s with 16). `--threads` overrides both.
- Peak RAM with a precomputed cache is the cache plus 40–105 MiB: each pin's
  row of lines stays in its own buffer. (Rows used to be copied into one
  buffer, which needed about twice the cache size, e.g. 771 MiB instead of
  430 MiB at 512 px / 400 pins.)
- At 1024 px, 4000 segments of a 1 px thread do not reach the no-improvement
  point; more segments or a wider thread are needed for comparable density.

Multi-colour, layered (CLI, `examples/images/mona-lisa.jpg`, 512 px, 300
pins, max 8000 steps, defaults: Auto order, 2 refinement passes). Wall
time; *before* is the code before the parallel changes of 2026-10-28
(fixed 32 threads, both orders one after the other, palette candidates
one after the other):

| run | before (s) | after (s) | final error |
|---|---|---|---|
| black only | 0.9 | 0.6 | 0.01181 |
| 4 given colours | 12.3 | 10.0 | 0.01235 |
| `--colors auto:4` (suggestion + run) | 26.2 | 13.5 | 0.01235 |

Results are identical before and after. Auto builds and refines both layer
orders side by side, each with half of the threads; palette suggestion runs
its small proxy optimizations one thread each, several at a time. One
layered run scales like single colour: 1 thread 62 s, 8 threads 14 s,
16 threads 12 s (both orders one after the other). More results in
ARCHITECTURE.md (*Multi-colour threads*).
