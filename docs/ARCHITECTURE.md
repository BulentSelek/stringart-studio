# Architecture and Models

Status: Phases 1–4 of the [specification](SPECIFICATION.md) are implemented,
except the optional CUDA backend. This document describes what is
implemented, the mathematical models, and the design decisions taken where
the specification left room for choice.

## Modules

```text
src/
├── core/                  stringart_core (static lib: Qt Core, Gui, Concurrent; no Qt Quick)
│   ├── GrayImage.h            float image buffer
│   ├── ImageProcessor.*       non-destructive preprocessing pipeline
│   ├── PinGeometry.*          circular, elliptical, rectangular, custom layouts; physical scale
│   ├── LineRasterizer.*       anti-aliased, ink-conserving line coverage
│   ├── LineCache.*            precomputed line coverage under a memory budget
│   ├── Compositing.*          thread compositing model + sRGB transfer
│   ├── ThreadRenderer.*       CPU renderer (preview and high-resolution export)
│   ├── ColorRenderer.*        multi-colour compositing and rendering
│   ├── PlaybackRenderer.*     winding playback (single and multi-colour)
│   ├── OptimizationConfig.*   parameters + validation
│   ├── OptimizationProblem.*  target, weights, pins; objective definition
│   ├── StringArtResult.*      result, progress snapshot, stop reasons
│   ├── ExportManager.*        PNG, CSV and SVG export
│   ├── WorkshopDocuments.*    pin template and winding guide PDFs, thread usage
│   └── ProjectFile.*          JSON project format, custom pin CSV import
├── algorithms/
│   ├── IOptimizer.h           common optimizer interface
│   ├── GreedyOptimizer.*      Algorithm A
│   ├── LayeredColorOptimizer.* multi-colour threads wound in layers (Algorithm B: layer refinement)
│   └── PaletteSuggester.*     thread colours from the image (multi-colour)
├── app/                   stringart_ui (QML module "StringArtStudio") + stringart-studio
│   ├── ProjectController.*    UI state and commands exposed to QML
│   ├── OptimizationWorker.*   std::jthread worker, pause/resume/cancel, throttled previews
│   ├── ImageStore.*           thread-safe image slots + QQuickImageProvider
│   ├── main.cpp
│   └── qml/                   Main, SettingsPanel, ImagePanel, ProgressPanel, PlaybackPanel,
│                              WindingView, ParamSlider, AccentButton, HelpLabel
└── cli/main.cpp           stringart-cli
tests/                     Qt Test suites (core + end-to-end UI)
benchmarks/                stringart-benchmark + run_benchmarks.sh
examples/                  deterministic synthetic test images
```

Dependency direction: `app -> algorithms/core`, `cli -> algorithms/core`.
Nothing in `core` or `algorithms` includes Qt Quick or QML headers.

## Coordinate system and frames

Working-image continuous pixel coordinates: pixel `(x, y)` covers
`[x, x+1) x [y, y+1)`, y points down. A `FrameSpec` (shape, pin count,
physical size or custom pins in mm) plus the working size (pixels on the long
side) defines the canvas: square for circles, the frame's aspect ratio
otherwise. Pins stay one pixel inside the canvas and are numbered clockwise
along the perimeter. Ellipses, rectangles and custom pins use one scale for
both axes, the largest that fits, and are centred, so the pins span exactly
the frame size in mm even when the canvas size had to be rounded. `PinLayout::mmPerUnit()` maps working units to mm.
The UI offers the working size as *Effort* (Run page): Low 384 px, Medium
512 px (default), High 1024 px; projects and the CLI (`--size`) may use any
size, shown as *Custom*.

| shape | placement | pin 0 |
|---|---|---|
| circular | equal angles on the inscribed circle | top |
| elliptical | equal arc length on the inscribed ellipse (arc length tabulated with ≥ 4096 samples) | top |
| rectangular | equal perimeter steps on the inset rectangle; corners only get a pin when the step divides the side | top-left corner |
| custom | CSV points (mm) in file order, uniformly scaled and centred | first row |

Connection rules: perimeter separation ≥ `minPinSeparation` (cyclic index
distance, so custom files must list pins in perimeter order) and
`PinLayout::canConnect`: on rectangles, pins on the same side are never
connected because the thread would lie along the frame edge. The
optimization mask is the convex hull of the pins (scanline-filled), i.e. the
area threads can reach.

Thread size is physical: the UI, projects (`threadDiameterMm`) and the CLI
(`--thread-mm`, default 0.3 mm) take the thread diameter on the frame; the
optimizer gets the equivalent width in working pixels
(`diameter / mmPerUnit`, stored as `threadWidth`), so changing the frame
size or the working resolution keeps the real thread. Typical threads are
thinner than a working pixel; the rasterizer's coverage then is the
fraction of the pixel the thread covers. Opacity (default 0.65) stays a
separate model parameter: real thread is close to opaque, lower values
soften overlaps. Measured on the synthetic portrait (512 px, 500 mm, 240
pins): 0.3 mm with opacity 1.0 gave error 0.0449 (1884 segments before no
line helped), opacity 0.65 gave 0.0389 (3042); on a Mona Lisa photo
opacity 1.0 was better (0.0185 vs 0.0226), so the right value depends on
the image.

## Preprocessing pipeline

`original QImage` (never modified)
→ `prepareBase`: crop (default: centred, output aspect), resample (Qt smooth
scaling), gray = Rec. 709 luminance computed in linear light, re-encoded to sRGB
→ `applyTone`: invert, contrast around 0.5, brightness offset, gamma `v^(1/γ)`
→ display-encoded working image `g` in [0, 1].

`prepareBase` first rotates (arbitrary angle, expanded canvas), then crops.
The crop is normalized to the rotated image and may extend beyond it (the
frame can be larger than the photo); the crop actually used
(`effectiveCrop`) is the largest rectangle with the canvas aspect ratio
centred in the requested crop, so the working image is never distorted. A
crop that does not overlap the image falls back to the whole image.

Padding outside the photo, rotation corners and transparent pixels are
carried as per-pixel `alpha` and become empty canvas (white target, no
thread) after the tone curve, so they stay empty even with Invert.
`containingCrop` computes the smallest centred crop whose frame contains the
whole photo: for rectangles the bounding box, for circles and ellipses the
ellipse through the photo's corners (√2 larger for a matching aspect), both
including the one-pixel pin margin. The UI's corner handle keeps the canvas
aspect; on ellipse and rectangle frames the side handles change it and set
the frame height accordingly.

The app works from a copy of the photo limited to 2048 px on the long side
(working sizes are ≤ 1024 px) so rotation and crop stay interactive; crop,
rotation and frame changes are debounced by 60 ms, tone changes apply
immediately. The original file bytes are kept for embedding in projects.

## Compositing model (shared by optimizer and renderer)

Each pixel stores a linear-light transmittance `T` (fraction of background
light still visible), initially 1. A segment covering fraction `c` of a pixel
with opacity `a` gives

```
T' = T · (1 − a·c)
```

Equivalently the optical density `D = −ln T` is additive over threads
(Beer–Lambert view of stacked semi-transparent layers). Overlaps darken
progressively and never go below zero. Final colour is a linear-light blend
`C = thread + (background − thread) · T`, encoded to sRGB.

Physical assumptions: threads are independent absorbing layers; scattering,
thread shadows, sheen and thread tension are ignored. `a` combines thread
darkness and how much of a working pixel a single thread covers, so it should
be tuned together with the working resolution (e.g. a 0.5 mm thread on a
500 mm frame at 512 px covers ~0.5 of a pixel).

The optimizer and renderer call the same `applyThread` and the same
rasterizer, so rendering the result sequence at scale 1 reproduces the
optimizer's buffer bit for bit (tested).

## Line rasterization

The thread is a band of width `w` pixels. The segment is walked along its
major axis one column at a time; at the column centre the band spans a
minor-axis interval of length `w / cos θ`, and each pixel's coverage is the
exact overlap of that interval with the pixel (capped at 1). Every column
receives exactly `w / cos θ`, so total ink is `w · length` independent of
angle and sub-pixel phase. (A first version point-sampled a distance profile
and was off by ±11 % at 45°; the coverage-integral test caught it.)

Endpoints are always passed in pin-index order so `(i, j)` and `(j, i)`
produce identical samples.

## Objective

```
E(T) = Σ_p w_p · (enc(T_p) − g_p)² / Σ_p w_p
```

`enc` is the sRGB transfer function (table-driven, `SrgbEncoder`), `g` the
display-encoded target and `w` per-pixel weights: 0 outside the convex hull
of the pins, otherwise `1 + k·e_p`. `e_p ∈ [0, 1]` is the edge strength
(Sobel magnitude, 3×3 box blur, divided by its 99th percentile, clamped) and
`k ≥ 0` the *edge emphasis*. `k = 0` is plain MSE. Larger `k` makes errors on
contours and features cost more, so more thread goes to eyes, mouth and
outlines at the expense of flat regions. Errors computed with different `k`
are not comparable.

Decision: compositing is physical (linear light), but the error is measured in
display-encoded space. Measuring in linear light was implemented first; it
under-weights dark regions (a mid-gray in sRGB is 0.21 in linear light) and
produced flat portraits whose hair and eyes were barely darker than the skin.
The encoded objective gave clearly better contrast on the same input.

## Algorithm A — greedy residual optimization (implemented)

State: `T`, its encoding `E = enc(T)` (kept in sync to save one table lookup
per sample), the current pin, and the set of used pin pairs.

Each iteration:

1. Candidates: pins `j` with perimeter separation ≥ `minPinSeparation` from the
   current pin and, unless repeats are allowed, an unused pair.
2. Score each candidate exactly, touching only its pixels:
   `ΔS = Σ_{p∈line} w_p [(enc(T_p(1 − a c_p)) − g_p)² − (E_p − g_p)²]`
   (factored as a difference of squares to avoid cancellation).
3. Pick the most negative `ΔS`; ties break on the lowest pin index.
4. Update `T` and `E` along the line, append the pin, accumulate length.
5. Stop on: max segments, max thread length (the next best segment would
   exceed it), no valid candidate, best `ΔS > −minImprovement`, or
   cancellation.

Complexity per iteration: `O(P · L)` with `P` valid candidates (≤ pin count)
and `L` covered pixels per line (≈ length · (w + 1)). Memory: `O(W·H)` plus the
optional line cache (8 bytes per covered pixel sample over all valid pairs).

Parallelism: candidates are split into contiguous chunks scored on a
`QThreadPool`; each candidate's score is computed sequentially and the
reduction is deterministic, so results are identical for any thread count and
for cached vs on-demand rasterization (tested).

Validation: tests compare the incremental `ΔS` against a full re-render +
full error evaluation for every candidate, and compare a complete run against
a brute-force reference optimizer that re-evaluates the whole image per
candidate.

The output is always a single continuous thread: segment `i` joins
`sequence[i]` and `sequence[i+1]`.

## Multi-colour threads

In multi-colour mode the palette (1–8 colours) and the board colour are part
of the optimization. Compositing ("over", linear RGB): a thread of colour `k`
covering a fraction `β = a·c` of a pixel shows its own colour there,
`C' = C·(1 − β) + k·β`, starting from the board colour. For one colour this
equals the single-colour model; with several colours the order matters
(later threads lie on top). Light threads can brighten a dark board (e.g.
white on grey). Objective: `Σ w Σ_c (enc(C_c) − g_c)²` against the
display-encoded RGB target, reported divided by `3 Σ w` (per-channel
weighted MSE).

Colour thread portraits (e.g. Ani and Andrey Abakumov's: black and white
plus a few colours, about 8000 lines) are wound colour by colour, so
`LayeredColorOptimizer` makes each colour one continuous thread wound
completely before the next: the spool changes only between layers.

- A single-colour layer maps the canvas below it to `x·T + k·(1 − T)`, a
  per-pixel affine map. For layer `c`, with `R` the composite of the layers
  below and the layers above folded into `F = A·in + B`, the picture is
  `F = A·(R·T_c + k_c·(1 − T_c)) + B`.
- A layer is built like Algorithm A (one continuous thread from the start
  pin), but every candidate line is scored by the exact change of the full
  RGB objective through that map, over the line's pixels only. With one
  black thread on white and a grey target this is exactly Algorithm A
  (tested), and each step equals a brute-force re-render search (tested).
- Segment budgets: the target is dithered (serpentine Floyd–Steinberg in
  linear RGB) to the palette plus the board colour; each colour's share of
  the dithered picture sets its budget. The first build uses half of each
  budget, bottom to top.
- Refinement (2 passes): every layer is rebuilt from scratch with the others
  fixed and may use any segments the others leave free; it is kept only if
  the error drops (block coordinate descent, error never increases). The
  held-back half thus goes where it helps most, not by area.
- Order: *Auto* builds light-to-dark and dark-to-light and keeps the better
  one; light-to-dark, dark-to-light and palette order can be forced.

The result is a list of steps `(colour, from pin, to pin)`, layer by layer;
`ColorRenderer::renderRgb` of it reproduces the optimizer's picture
(tested). Exports: CSV with `color` / `color_hex` columns and per-colour
cumulative length, SVG with one stroke colour per line, project/JSON with
`color.palette`, `color.layerOrder` and `result.steps` (per-colour
continuity is validated on load).

Measured (CLI, 512 px, 240 pins, 6000 steps, palette black, white, red,
blue, yellow on white, 2026-10-27; error = per-channel weighted MSE). The
first column is the earlier interleaved optimizer (one global greedy over
all colours, spool changes at almost every step), removed since:

| image | interleaved (removed) | layered, first build only | layered | spool changes |
|---|---|---|---|---|
| red / blue halves, black disk | 0.03216 (5.4 s) | 0.05475 | 0.03054 (11 s) | 3 |
| sunset gradient, dark disk | 0.00961 (5.9 s) | 0.01918 | 0.00917 (11 s) | 4 |
| colour plasma | 0.01609 (6.4 s) | 0.02052 | 0.01728 (10 s) | 4 |

Tried and dropped: layers that match only their own dithered density map
(errors 0.049–0.059, grey disks) and fixed budgets by area (dark details
starve).

### Palette suggestion

`PaletteSuggester` proposes thread colours for a given image, frame, board
colour and run settings (UI: *From image … Suggest*; CLI: `--colors auto:N`).

1. Candidates: with "over" compositing a pixel explained by one thread
   colour lies on the ray from the board colour towards that colour, so the
   directions of the target pixels from the board colour are clustered
   (weighted spherical k-means, N + 2 clusters, deterministic seeding). Each
   cluster gives the most saturated colour on its ray and the "just deep
   enough" colour (95th percentile distance / 0.7); black (white on a dark
   board) is always a candidate.
2. Selection by the real objective: colours are added one at a time, each
   the candidate with the lowest error of a `LayeredColorOptimizer` run on a
   proxy (canvas 112 px, at most 120 pins, segments scaled to the same thread
   density, the run's layer order, one refinement pass), then one pass tries
   to replace each chosen colour by every other candidate. This captures
   mixtures, which clustering alone misses. Near-identical candidates are
   merged. The proxy needs the layer order and a refinement pass: without
   them it ranked palettes differently from the full run (it preferred two
   reds over red and blue on the halves image, and purple over orange on the
   sunset).

Measured (CLI, 512 px, 200 pins, 3000 steps, synthetic images,
2026-10-27; error of the full layered run):

| image | black, red, blue (preset) | auto:3 | suggested colours | suggestion time |
|---|---|---|---|---|
| red / blue halves, black disk | 0.12571 | 0.11048 | black, red, blue | 4.7 s |
| sunset gradient, dark disk | 0.05162 | 0.04705 | black, orange, yellow | 4.7 s |
| colour plasma | 0.09488 | 0.09556 | black, dark blue, brown | 5.3 s |

On the plasma image the suggestion is no better than the preset: on a white
board darkening dominates the objective, so black does most of the work. A
coloured board can matter more than the thread colours; the suggester keeps
the chosen board colour. The selection is greedy on a proxy, so it is a good
starting point, not a guaranteed optimum.

## Winding playback

`PlaybackRenderer` replays a finished sequence; it never re-optimizes. It
stores the transmittance buffer every `interval` segments (interval chosen so
the checkpoints fit 64 MiB, at least 16 segments), so seeking replays at most
one interval and stepping forward replays only the new segments. `seek(n)`
equals `ThreadRenderer::renderTransmittance(..., segmentLimit = n)` bit for
bit (tested). The UI's bottom panel offers a timeline, single steps (Left /
Right keys), play / pause (Space) at 1 – 1000 segments per second, jump to a
segment, the current source and destination pins, a red highlight of the
current segment, and PNG export of the current frame at the export size.

## Workshop documents and winding mode

`WorkshopDocuments` (core, QPainter on QPdfWriter) produces what is needed
to build the piece by hand:

- **Pin template** (`writePinTemplatePdf`): the frame at 1:1 scale with a
  drill crosshair and ring per pin, radial pin numbers (every pin, or every
  2nd / 5th / 10th / 25th ... pin when they would overlap; `pinLabelStep`),
  pin 0 in red, the frame outline, a centre mark and a 100 mm scale bar to
  check that the printer did not rescale. Output is either one sheet of the
  frame size (plotter / print shop) or tiles on A4, A3 or Letter. Tiles
  share a 15 mm strip; `planTiles` centres the grid on the frame and picks
  the orientation with fewer sheets. Each sheet carries its name (row
  letter, column number), a sheet map, a dashed glue line in the middle of
  each shared strip and round registration marks on it; the marks are drawn
  in frame coordinates, so they print at the same frame position on both
  neighbouring sheets, and they are kept 8 mm away from pins.
- **Winding guide** (`writeWindingGuidePdf`): A4 pages with the frame
  summary, thread per colour (start pin, segments, pin-to-pin length and
  length with a 10% reserve), then every step in order with a tick box,
  grouped in tens. Multi-colour rows show the colour and the source pin
  (each colour continues from where its own thread stopped); thread changes
  are shaded.
- **Thread usage** (`threadUsage`): segments and length per palette colour,
  shown under *Thread length* in the UI.

**Winding mode** (`WindingView.qml`) is a full-window view on top of the
playback position: step n of N, the source pin, the target pin in very large
type, the next four targets, a "change thread" banner for multi-colour
results, and the frame with every pin, numbers every ten pins and the
current segment. Next: Space, Right, Down, Enter or Page Down (USB foot
pedals usually send Page Down); back: Left, Up, Backspace or Page Up. An
optional timer advances automatically. When Qt TextToSpeech is installed
(optional at build time), the target pin can be read aloud. The position is
stored in QSettings under a hash of the result's steps, so reopening the
same project (or recomputing the same result) resumes winding where it
stopped.

## Undo, presets and run comparison

- **Undo / redo** (Ctrl+Z, Ctrl+Shift+Z, toolbar arrows): every parameter
  change, crop and rotation included, restarts a 400 ms timer; when it fires,
  a snapshot of all parameters (the project JSON without image) is pushed
  if it differs from the current entry. Quick successive edits (a slider
  drag) therefore form one step, and undo first commits a pending edit.
  Applying a snapshot is not recorded again. The history (200 steps) starts
  over when an image or project is opened. Results are not part of it.
- **Presets** (toolbar menu): named parameter sets without crop and
  rotation, stored in the application settings as project JSON, so they
  survive restarts and are validated on load. Loading one is undoable.
- **Runs** view: each finished run (and each refinement) of the current
  image is kept with its image, statistics and settings, up to 12. Two runs
  are shown side by side with the relative error difference; *Use settings*
  restores a run's parameters (undoable). Opening another image clears the
  list.

## Residual visualization

`ThreadRenderer::residualImage` maps `enc(T) − g` to a diverging image: red
where the result is lighter than the target (more thread would help), blue
where it is darker, white where they match, gray where the weight is zero.
The worker publishes it with each preview. In the UI it is a mode of the
String art view (*Thread / Error map / Compare*), not a separate tab. For
multi-colour results `ColorRenderer::residualImage` shows the luminance
difference only; hue errors (e.g. orange instead of red) are not visible
in it.

## Tried and not adopted: single-thread sequence refinement

A local search on the finished single-colour thread (relocate, delete or
insert one pin; exact change of the objective by dividing out removed
segments; continuity kept) was implemented and tested, then removed: on a
greedy result it lowered the error by only ≈ 0.02–0.07 % for 2–7 s
(e.g. 0.126971 → 0.126966), because greedy is already close to a local
optimum for these moves. It helped only on sequences that did not come from
greedy, which the program never produces. Refinement that matters lives in
the multi-colour layers (*Layered colours* above: rebuilding each layer with
the others fixed lowered the error by 16–52 % against the first build).

### Tried and not adopted: greedy lookahead

A two-step lookahead for Algorithm A was implemented and measured
(2026-10-27, CLI, 512 px, 200 pins): the B best next pins were re-ranked by
their own gain plus the best gain of the step after them, committing one
step at a time. It did not help:

| image | segments | error, no lookahead | error, lookahead 4 | runtime |
|---|---|---|---|---|
| synthetic portrait | 3000 | 0.037358 | 0.037365 | 0.47 s → 2.42 s |
| synthetic disk | 1000 | 0.084724 | 0.084795 | 0.19 s → 0.88 s |
| sunset (grey) | 3000 | 0.024964 | 0.024971 | 0.51 s → 2.53 s |

Lookahead 2 and 8 on the portrait gave 0.037357 and 0.037371. With
hundreds of nearly equivalent candidate lines per step, a one-step-deeper
view does not change the outcome noticeably, so the code was removed
rather than offered as a slower option with no benefit.

### Tried and not adopted: least-squares line weights

A stage that gave the result's lines continuous weights by bounded least
squares in optical density (projected coordinate descent, KKT-verified)
was implemented and removed: e.g. error 0.052 with ideal weights vs 0.083
for the real thread at 1200 segments, but fractional weights cannot be
wound, it worked for single-colour results only, and the number did not
help anyone build a piece.

## Threading and UI

- `OptimizationWorker` runs the optimizer on a `std::jthread`. The progress
  callback publishes a preview at most every 120 ms: it converts `T` to a
  `QImage` (working resolution) on the worker thread and stores it in
  `ImageStore`; the GUI thread only receives a `progressed()` signal and
  reads a small snapshot struct.
- Pause blocks inside the progress callback on a
  `std::condition_variable_any` with the run's `stop_token`, so cancel also
  wakes a paused run. A cancelled run keeps its partial result.
- QML displays images via `image://studio/<slot>/<revision>`; the revision
  only busts QML's cache. No pixel buffers cross into QML properties.
- PNG export renders on `QtConcurrent::run` and reports back with
  `QFuture::then(this, …)`.
- `ProjectController` is created in C++ and passed to `Main.qml` as a
  required property; the `ImageStore` is shared via `std::shared_ptr` so
  destruction order between engine and controller is safe.

## Export formats

- PNG: re-rendered with the long side at the requested size (pin coordinates
  and thread width scaled; opacity unchanged, so a box-downsampled export
  approximates the working preview — tested).
- CSV: `segment,from_pin,to_pin,from_x,from_y,to_x,to_y,length,cumulative_length`
  with `_mm` or `_px` suffixes. Pins are 0-based in layout order;
  coordinates are relative to the canvas top-left corner, y down.
- SVG: millimetre units (`viewBox` = physical canvas), one `<line>` per
  segment with `stroke-opacity` = thread opacity so overlaps compound, plus
  pin markers. SVG viewers composite in sRGB rather than linear light, so
  mid-tones look somewhat darker than the PNG.
- Pin template and winding guide: PDF, see *Workshop documents*.
- JSON / project (`.sasproj`): format `stringart-studio-project`, version 1:
  `frame`, `workingSize`, `image` (path, rotation, crop, tone, optionally the
  original file bytes as base64), `optimization`, `render`, `result`
  (sequence and statistics). The project file embeds the image; the JSON
  export omits it and adds a `pins` array with every pin's position in mm.
  Loading validates every field and rejects sequences with out-of-range pins.
  The rendered result is not stored: it is re-rendered from the sequence
  (deterministic), and re-running the saved settings reproduces the same
  sequence (tested). When a result exists, the project stores the settings
  of the run that produced it (current colours), not later edits.
