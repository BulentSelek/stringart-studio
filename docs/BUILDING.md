# Building and Running

## Requirements

- CMake ≥ 3.21 and Ninja
- C++20 compiler (tested: GCC 16.2; MSVC 2022 / clang should work but are untested)
- Qt ≥ 6.8 with Core, Gui, Concurrent, Qml, Quick, QuickControls2, Widgets, Test
- Optional: Qt TextToSpeech (spoken pin numbers in winding mode; detected automatically)
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

1. **Frame** — shape (circle, ellipse, rectangle or custom CSV), physical
   size, position on the photo (rotation, *Fit whole photo*, *Reset crop*),
   pin count, minimum separation; *Advanced*: start pin. This page shows the
   *Original* view, where the yellow frame is moved and resized on the photo
   (it may extend beyond it).
2. **Tone** — brightness, contrast, gamma, invert, histogram; shows the
   *Processed* view.
3. **Thread** — opacity, diameter in mm (with the equivalent working
   pixels) and colours. *Multi-colour threads* turns the palette (presets,
   *From image … Suggest*, add / edit / remove up to 8 colours, board colour,
   layer order) into part of the optimization; each colour becomes its own
   continuous thread, wound layer by layer.
4. **Run** — effort (Low / Medium / High working resolution), max.
   segments, edge emphasis; *Advanced*: max. thread length, repeated lines,
   refinement passes and reach.

Press **Start** on the right (Pause / Resume / Cancel while it runs). The
*String art* view updates live; its mode selector shows the thread, the
*Error map* (red: lighter than the target, blue: darker) or *Compare* (the
target left of a draggable divider). The mouse wheel zooms around the
cursor, drag pans, **Fit** resets, **Pins** overlays numbered pins. After a
run, **Refine sequence** runs a local search on the finished thread that
moves, removes or inserts single pins where that lowers the error; the
thread stays continuous. The *Runs* view keeps the finished runs of the
current image side by side. Toolbar: undo / redo of parameter changes
(Ctrl+Z, Ctrl+Shift+Z) and named presets.

Exports on the right: PNG, winding CSV (mm or px), SVG, JSON, and under
*Workshop* a 1:1 pin template PDF and an A4 winding guide PDF.

After a run, the bottom *Winding* panel replays the sequence: drag the
timeline, step with ◀ ▶ (or the Left / Right keys), play / pause with Space,
choose the speed, jump to a segment, and export the current frame. The
current segment is highlighted in red with its source and destination pins.
**Winding mode** opens a full-window step-by-step view for winding the real
piece (keys or a foot pedal, optional auto-advance and speech).

Custom pin CSV: one pin per line, `x,y` in mm (comma, semicolon, tab or
spaces), in perimeter order; an optional header naming `x`/`y` columns is
honoured.

## Command line

```bash
./build/src/cli/stringart-cli portrait.jpg --pins 240 --segments 4000 \
    --png out.png --png-size 2048 --csv out.csv
./build/src/cli/stringart-cli portrait.jpg --shape ellipse --width-mm 400 --height-mm 600 \
    --edge-emphasis 1.5 --rotate 0 --svg out.svg --json out.json
./build/src/cli/stringart-cli portrait.jpg --colors "#000000,#7a4a2a,#c9a27a" --background "#f4ecd8" \
    --png colour.png --csv colour.csv
./build/src/cli/stringart-cli photo.jpg --colors "#000000,#ffffff,#c0392b,#1f4e8c,#f0c000" \
    --segments 8000 --layer-order auto --png colour.png
./build/src/cli/stringart-cli photo.jpg --colors auto:3 --png colour.png
./build/src/cli/stringart-cli portrait.jpg --template-pdf pins.pdf --template-paper a4 \
    --guide-pdf winding.pdf
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
`tst_project`, `tst_playback`, `tst_sequence`, `tst_leastsquares`,
`tst_color`, `tst_ui`. A sanitizer build also passes:

```bash
cmake -S . -B build-asan -G Ninja -DCMAKE_BUILD_TYPE=Debug -DSTRINGART_BUILD_BENCHMARKS=OFF \
    -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer" \
    -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined"
cmake --build build-asan && (cd build-asan && ctest)
```

Things that need a person (dialogs, drag and drop, hover, restart) are listed
in `docs/MANUAL_TESTS.md`. The UI test runs headless (`QT_QPA_PLATFORM=offscreen`); set
`STRINGART_UI_SCREENSHOT=/path/shot.png` to save a screenshot of the window
after a full run (`STRINGART_UI_SCREENSHOT2` for the Phase 2 workflow).

## Benchmarks

```bash
benchmarks/run_benchmarks.sh build            # 512/1024 px x 200/400/800 pins
build/benchmarks/stringart-benchmark --help   # single configuration
```

Results from a reference machine are in `docs/BENCHMARKS.md`.
