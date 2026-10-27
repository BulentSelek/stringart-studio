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

File actions (open image, open/save project) are in the toolbar; images can
also be dropped on the window. File dialogs reopen in the last folder used
for images, projects, pin CSVs and exports respectively. Window size and
position, the selected parameter page and all parameters (frame, pins,
thread, tone, run and export options) are restored on the next start; crop
and rotation are not, as they belong to an image. Everything is stored in the
user settings, e.g. `~/.config/StringArtStudio/String Art Studio.conf` on
Linux. Parameters on the left are grouped by workflow
step, each with secondary options under *Advanced* and an explanation on
hover (?):

1. **Image** — tone (brightness, contrast, gamma, invert, histogram),
   rotation and crop. The crop frame is edited on the *Original* view; it may
   extend beyond the photo, and **Fit whole photo** puts the entire photo
   inside the frame.
2. **Frame** — shape (circle, ellipse, rectangle or custom CSV), physical
   size, pin count, minimum separation; *Advanced*: start pin. The
   *Processed* view shows the frame outline.
3. **Thread** — opacity, width (with the equivalent physical thread
   diameter) and colours.
4. **Run** — working size, max. segments, edge emphasis; *Advanced*: max.
   thread length, repeated lines.

Selecting *Image* or *Frame* switches the centre view to *Original* or
*Processed*. Press **Start** on the right; the *String art* and *Residual*
views update live, with Pause / Resume / Cancel. The mouse wheel zooms around
the cursor, drag pans, **Fit** resets, **Pins** overlays pin markers. Export
PNG, the winding CSV (mm or px), SVG or JSON on the right.

After a run, the bottom *Winding* panel replays the sequence: drag the
timeline, step with ◀ ▶ (or the Left / Right keys), play / pause with Space,
choose the speed, jump to a segment, and export the current frame. The
current segment is highlighted in red with its source and destination pins.

Custom pin CSV: one pin per line, `x,y` in mm (comma, semicolon, tab or
spaces), in perimeter order; an optional header naming `x`/`y` columns is
honoured.

## Command line

```bash
./build/src/cli/stringart-cli portrait.jpg --pins 240 --segments 4000 \
    --png out.png --png-size 2048 --csv out.csv
./build/src/cli/stringart-cli portrait.jpg --shape ellipse --width-mm 400 --height-mm 600 \
    --edge-emphasis 1.5 --rotate 0 --svg out.svg --json out.json
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
`tst_project`, `tst_ui`. The UI test runs headless (`QT_QPA_PLATFORM=offscreen`); set
`STRINGART_UI_SCREENSHOT=/path/shot.png` to save a screenshot of the window
after a full run (`STRINGART_UI_SCREENSHOT2` for the Phase 2 workflow).

## Benchmarks

```bash
benchmarks/run_benchmarks.sh build            # 512/1024 px x 200/400/800 pins
build/benchmarks/stringart-benchmark --help   # single configuration
```

Results from a reference machine are in `docs/BENCHMARKS.md`.
