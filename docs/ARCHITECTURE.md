# Architecture and Models

Status: Phase 1 (functional MVP) and Phase 2 (quality improvements). This document describes what is implemented,
the mathematical models, and the design decisions taken where the
specification left room for choice.

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
│   ├── ColorGreedyOptimizer.* multi-colour greedy
│   ├── GreedyOptimizer.*      Algorithm A
│   ├── LeastSquaresOptimizer.* Algorithm B (virtual)
│   └── SequenceOptimizer.*    Algorithm C
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
along the perimeter. `PinLayout::mmPerUnit()` maps working units to mm.

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
(later threads lie on top), so the result is a **global winding order** of
steps `(colour, from pin, to pin)` in which each colour forms one continuous
thread from the start pin. Winding follows the step order, switching spools
when the colour changes; each thread stays attached at its current pin.
Light threads can brighten a dark board (e.g. white on grey).

`ColorGreedyOptimizer` scores every valid line from every colour's current
pin exactly over the line's pixels against the display-encoded RGB target
(objective `Σ w Σ_c (enc(C_c) − g_c)²`, reported as a per-channel weighted
MSE), with deterministic ties (colour, then pin). Tests: identical to the
single-colour optimizer for one black thread, identical to a brute-force
re-render search, per-colour continuity, and colour placement on a two-colour
image. Exports: CSV with `color` / `color_hex` columns and per-colour
cumulative length, SVG with one stroke colour per line, project/JSON with
`color.palette` and `result.steps` (continuity is validated on load).
Sequence refinement and the virtual least-squares reconstruction are
single-colour only.

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

## Residual visualization

`ThreadRenderer::residualImage` maps `enc(T) − g` to a diverging image: red
where the result is lighter than the target (more thread would help), blue
where it is darker, white where they match, gray where the weight is zero.
The worker publishes it with each preview.

## Algorithm C — sequence refinement (implemented)

`SequenceOptimizer` (an `IOptimizer`) improves an existing continuous
sequence by local search. Moves at position *i* with neighbours
`a = s[i-1], b = s[i], c = s[i+1]`:

| move | change | when |
|---|---|---|
| relocate | `(a,b),(b,c) → (a,p),(p,c)`, `p` within ± *reach* pins of `b` (all pins if 0) | always |
| delete | `(a,b),(b,c) → (a,c)` | optional (`changeSegmentCount`) |
| insert | `(a,b) → (a,p),(p,b)`, any `p` | optional, while below `maxSegments` |

The start pin is fixed and every move keeps consecutive segments sharing an
endpoint, so the result is still one continuous thread; moves respect the
separation, `canConnect`, repeated-line and maximum-length rules. The change
of the objective is exact: with the multiplicative model a segment is removed
by dividing by `(1 − a·c)` and added by multiplying, so only the pixels of the
affected lines are evaluated (requires opacity < 1). The best improving move
per position is applied (deterministic ties), positions are swept in order,
and sweeps repeat until one finds nothing or *passes* is reached; `T` is
re-rendered exactly after each sweep. Tests check exactness (after
convergence, no single relocation improves the fully recomputed error),
continuity, constraints and thread-count independence.

Complexity per sweep: `O(n · K · L)` with `K` = 2·reach relocations (+ all
pins for insertions while below the budget).

Measured effect (512 px synthetic portrait, 240 pins): on a greedy result
that used its full 3000-segment budget, refinement lowers the error by
≈ 0.02 % (reach 20, 2 s) to 0.07 % (all pins, 5 sweeps, 7 s). Greedy is
already close to a local optimum for these moves; the gain is larger on
sequences that did not come from greedy (≈ 10 % or more on a naive
sequence in the tests) and when the segment budget leaves room for
insertions. Available as *Refine sequence* in the UI and `--refine-passes` /
`--refine-neighborhood` in the CLI.

## Algorithm B — least-squares line weights (implemented, virtual)

`LeastSquaresOptimizer` fixes the candidate set to the distinct lines of the
current result and gives each a continuous weight `x_l ∈ [0, xmax]`
(default 4; initial value = how often the line occurs). In optical density
threads add up exactly:

```
d_l(p) = −ln(1 − a·c_l(p))          density of one thread along line l
D(p)   = Σ_l x_l d_l(p),  T = e^(−D)
D*(p)  = −ln(max(t(p), 10⁻³))       t = linear-light target
minimize Σ_p w_p (D(p) − D*(p))²    subject to 0 ≤ x_l ≤ xmax
```

This convex box-constrained QP is solved by cyclic projected coordinate
descent (`x_l ← clamp(x_l − g_l/h_l, 0, xmax)`), which decreases the
objective monotonically; tests verify the KKT conditions. Cost per sweep:
`O(Σ line lengths)`; typically < 0.1 s.

**This is not a manufacturable result.** Fractional weights cannot be wound
and the continuity of the thread is ignored. The UI therefore shows it only
in a separate *Virtual (LS)* view with a banner, next to the error of the
actual thread (e.g. 0.052 virtual vs 0.083 thread for 1200 segments): the gap
measures how much the integer, continuous-thread constraint costs for that
line set. Exports always use the real sequence. Note that the fit is in
density space, not in the sRGB objective, so it is an informative bound
rather than an exact optimum of the reported error.

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
