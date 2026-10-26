#!/usr/bin/env bash
# Runs the benchmark matrix (512/1024 px x 200/400/800 pins), one process per
# configuration so that peak RAM is measured per configuration.
# Usage: benchmarks/run_benchmarks.sh [build-dir] [extra benchmark args...]
set -euo pipefail
build="${1:-build}"
shift || true
bench="$build/benchmarks/stringart-benchmark"
export QT_QPA_PLATFORM=offscreen

"$bench" --header
for size in 512 1024; do
    for pins in 200 400 800; do
        "$bench" --size "$size" --pins "$pins" "$@"
    done
done
