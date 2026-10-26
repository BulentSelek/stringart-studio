# Building and Running

## Requirements

- CMake ≥ 3.21 and Ninja
- C++20 compiler (tested: GCC 16.2; MSVC 2022 / clang should work but are untested)
- Qt ≥ 6.8 with Core, Gui, Concurrent, Qml, Quick, QuickControls2, Test
  (tested with Qt 6.12.0). WebP input needs the Qt Image Formats plugin.

## Build

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Options (all `ON` by default): `STRINGART_BUILD_APP`, `STRINGART_BUILD_TESTS`,
`STRINGART_BUILD_BENCHMARKS`. With `STRINGART_BUILD_APP=OFF` only Qt Core/Gui/
Concurrent are needed (engine, CLI, core tests).

On Windows, configure from a "x64 Native Tools" prompt with
`-DCMAKE_PREFIX_PATH=C:/Qt/6.8.x/msvc2022_64` and run `windeployqt` on the
executable before distributing.

## Run the application

```bash
./build/src/app/stringart-studio [optional-image-path]
```

1. Open an image (button or drag and drop).
2. Adjust preprocessing on the left; check the *Processed* tab and histogram.
3. Choose pin count, minimum separation, working size, max segments, thread
   opacity and width.
4. Press **Start**. The *String art* tab updates live; Pause / Resume /
   Cancel are on the right. The mouse wheel zooms around the cursor, drag pans,
   **Fit** resets, **Pins** overlays pin markers with indices.
5. Export PNG (choose size) and the winding CSV (mm or px).

## Command line

```bash
./build/src/cli/stringart-cli portrait.jpg --pins 240 --segments 4000 \
    --png out.png --png-size 2048 --csv out.csv
./build/src/cli/stringart-cli --help
```

Sample inputs (synthetic portrait and disk):

```bash
./build/examples/make-sample-images samples/
```

## Tests

```bash
cd build && ctest --output-on-failure
```

Suites: `tst_geometry`, `tst_imageprocessor`, `tst_optimizer`, `tst_export`,
`tst_ui`. The UI test runs headless (`QT_QPA_PLATFORM=offscreen`); set
`STRINGART_UI_SCREENSHOT=/path/shot.png` to save a screenshot of the window
after a full run.

## Benchmarks

```bash
benchmarks/run_benchmarks.sh build            # 512/1024 px x 200/400/800 pins
build/benchmarks/stringart-benchmark --help   # single configuration
```

Results from a reference machine are in `docs/BENCHMARKS.md`.
