# Project: String Art Studio — High-Performance C++/Qt Application

## 1. Objective

Develop a professional, cross-platform String Art generator.

The application must convert ordinary photographs into high-quality string art by optimizing the sequence of straight threads connecting predefined anchor points (pins/nails).

This must be a fully functional application, not a proof of concept or UI mockup.

The software must be independently implemented. Do not copy proprietary source code, branding, or UI assets from other string art software.

Prioritize:

1. Output image quality
2. Algorithmic efficiency
3. Modern, responsive Qt Quick UI
4. Maintainable C++ architecture
5. Reproducibility and correctness

## 2. Technology Stack

- Language: C++20
- Framework: Qt 6.8 or newer
- UI: QML
- Build system: CMake, Ninja
- Platforms: Linux and Windows
- Image processing: Qt QImage initially
- Concurrency: std::jthread, QtConcurrent, or carefully designed worker threads
- Testing: Qt Test
- GPU acceleration: Optional, future implementation

Use modern C++ practices, RAII, smart pointers, const correctness, and clear ownership rules.

Avoid unnecessary dependencies.

Keep the computational engine independent of Qt Quick/QML so it can also be used as a command-line tool or library.

## 3. Application Features

### 3.1 Image Input

Support:

- JPEG, PNG, BMP and WebP where Qt plugins are available
- Drag-and-drop loading
- Image cropping
- Rotation
- Brightness and contrast
- Gamma adjustment
- Grayscale conversion
- Inversion
- Optional histogram visualization

Provide a non-destructive preprocessing pipeline.

Maintain the original image separately from the processed working image.

### 3.2 Pin Geometry

Support the following layouts:

- Circular
- Elliptical
- Rectangular perimeter
- Custom pin positions imported from CSV

Parameters:

- Number of pins
- Canvas width and height
- Pin distribution
- Starting pin
- Minimum separation between connected pins
- Maximum total thread length
- Maximum number of thread segments

For circular layouts, support at least 100–1000 pins, with higher counts when practical.

Coordinates must be represented in a consistent floating-point coordinate system.

### 3.3 String Art Optimization Engine

Implement multiple optimization strategies behind a common C++ interface.

#### Algorithm A: Greedy Residual Optimization

This is the initial algorithm and must be fully implemented.

At every iteration:

1. Evaluate valid candidate lines starting from the current pin.
2. Estimate how much each candidate improves the image reconstruction.
3. Select the best candidate according to a defined error function.
4. Update the accumulated thread coverage and residual error.
5. Append the selected endpoint to the pin sequence.
6. Repeat until reaching a stopping condition.

Use an additive thread-opacity model rather than simply drawing opaque black lines.

Support:

- Thread opacity
- Thread width
- Repeated lines
- Minimum pin separation
- Anti-aliased line rasterization
- Early stopping
- Deterministic tie-breaking
- Configurable thread count

Use a mathematically defined objective, initially weighted mean squared error.

Avoid evaluating every candidate by repeatedly rendering the full image.

Precompute line pixel coverage or use efficient incremental line scoring.

#### Algorithm B: Layer Refinement (multi-colour)

Multi-colour results are wound layer by layer: each colour is one continuous thread wound completely before the next.

After a first build of all layers, refine each layer again while the other layers stay fixed (block coordinate descent), scoring every line by the exact change of the full colour objective through the layers below and above.

Keep a rebuilt layer only if the error decreases, so refinement never makes the result worse.

Optimization must preserve the continuous-thread constraint within each colour: consecutive segments of a colour share an endpoint.

Clearly document the objective function and complexity.

## 4. Rendering Engine

Implement a CPU renderer that simulates physical thread accumulation.

Requirements:

- Adjustable thread thickness
- Adjustable opacity
- Subpixel coordinates
- Anti-aliasing
- Multiple overlapping segments
- Configurable background and thread colors
- High-resolution export

Do not assume that overlapping threads behave like ordinary opaque drawing.

Define a consistent compositing model and use it for both optimization and preview.

A simplified Beer–Lambert-inspired optical-density model may be explored, but its physical assumptions must be documented.

The output must approximate how real physical string art will appear.

## 5. Qt Quick User Interface

Develop a clean desktop interface using QML.

Suggested layout:

**Left panel**
- Image loading
- Cropping and preprocessing
- Pin geometry settings
- Optimization parameters
- Thread settings

**Center panel**
- Original image
- Preprocessed image
- Generated string art
- Zoom and pan
- Pin markers and indices
- Interactive comparison

**Right panel**
- Optimization progress
- Current segment count
- Reconstruction error
- Processing time
- Estimated total thread length
- Start, pause, resume, cancel

**Bottom panel**
- Timeline
- Segment navigation
- Animation playback
- Playback speed

Use Qt Quick Controls with a modern, consistent interface.

Do not perform expensive computations on the GUI thread.

Use a C++ image provider, custom QQuickItem, or another justified Qt Quick rendering approach.

Avoid transferring entire image buffers to QML for every optimization iteration.

Preview updates should be throttled to preserve responsiveness.

## 6. Performance Requirements

Design the engine for high-performance CPU execution.

Implement and benchmark:

- Precomputed pin coordinates
- Rasterized line coverage caching
- Efficient residual updates
- Candidate pruning
- Multithreaded candidate evaluation
- Memory-efficient data structures
- Avoidance of redundant image copies

Start with a correct scalar reference implementation.

Optimize only after implementing correctness tests and benchmarks.

Use profiling to identify actual bottlenecks.

Evaluate SIMD acceleration only where measurable benefits exist.

Optional later backend:

- CUDA for NVIDIA GPUs
- Backend abstraction that does not affect the core optimization API

Do not make CUDA a dependency for the initial functional release.

Provide performance benchmarks at 512×512 and 1024×1024 working resolutions, with 200, 400 and 800 pins.

Report actual runtime, CPU utilization, peak RAM usage and reconstruction error. Do not invent benchmark results.

## 7. Export Formats

Implement:

### PNG
High-resolution final artwork.

### SVG
Vector representation of pins and thread segments.

### CSV
Complete ordered pin connection sequence.

### JSON
Project configuration and ordered pin sequence.

### Project File
Save and restore all parameters, image reference or embedded input, and optimization results.

CSV should contain:

- Segment index
- Source pin
- Destination pin
- Source coordinates
- Destination coordinates
- Segment length
- Cumulative thread length

Provide an option to export physical coordinates in millimeters.

## 8. String Winding Animation

Implement playback of the calculated sequence.

Features:

- Step-by-step visualization
- Forward and backward navigation
- Adjustable playback speed
- Display current source and destination pins
- Jump to a specified segment
- Optional thread path highlighting

Playback must use the existing computed sequence, not recalculate optimization.

Support exporting individual frames if feasible.

## 9. Software Architecture

Separate the application into modules:

```text
StringArtStudio/
├── CMakeLists.txt
├── src/
│   ├── core/
│   │   ├── ImageProcessor.h/.cpp
│   │   ├── PinGeometry.h/.cpp
│   │   ├── ThreadRenderer.h/.cpp
│   │   ├── LineCache.h/.cpp
│   │   ├── StringArtEngine.h/.cpp
│   │   ├── OptimizationConfig.h
│   │   └── StringArtResult.h
│   ├── algorithms/
│   │   ├── IOptimizer.h
│   │   ├── GreedyOptimizer.h/.cpp
│   │   └── LayeredColorOptimizer.h/.cpp
│   ├── application/
│   │   ├── ProjectController.h/.cpp
│   │   ├── OptimizationWorker.h/.cpp
│   │   └── ExportManager.h/.cpp
│   ├── ui/
│   │   ├── main.cpp
│   │   └── qml/
│   │       ├── Main.qml
│   │       ├── ImagePanel.qml
│   │       ├── SettingsPanel.qml
│   │       ├── ProgressPanel.qml
│   │       └── PlaybackPanel.qml
│   └── cli/
│       └── main.cpp
├── tests/
├── benchmarks/
├── examples/
└── docs/
```

This is a suggested structure, not a requirement to create unnecessary empty classes.

Clearly separate:

- Domain models
- Optimization
- Image rendering
- Asynchronous execution
- UI state

No optimization algorithm may directly depend on QML components.

## 10. Testing

Write automated tests covering:

- Pin coordinate generation
- Line rasterization
- Correctness of opacity accumulation
- Residual error updates
- Deterministic optimization results
- Continuous valid pin sequences
- CSV/SVG export
- Save/load round trips
- Cancellation
- Small synthetic image reconstruction

For the greedy algorithm, compare optimized incremental scoring against a brute-force reference implementation on small inputs.

Include tests that verify error reduction under the documented objective function.

Test edge cases such as blank images, single-color images, zero opacity, invalid pin count and insufficient valid connections.

## 11. Development Phases

### Phase 1 — Functional MVP

Deliver:

- Qt Quick application
- Image loading
- Grayscale processing
- Circular pin layout
- Greedy optimization
- Thread rendering
- Progress reporting
- PNG and CSV export

This phase must compile and run successfully.

Do not proceed to complex algorithms before validating the MVP.

### Phase 2 — Quality Improvements

- Adjustable opacity and thread thickness
- Additional pin layouts
- Residual visualization
- Improved optimization
- SVG export
- Project save/load

### Phase 3 — Performance

- CPU profiling
- Line caching improvements
- Candidate evaluation parallelism
- Memory optimizations
- Performance benchmarks

### Phase 4 — Advanced Features

- Colored strings wound in layers, with layer refinement
- Playback
- Optional CUDA backend

## 12. Agent Execution Rules

You are an autonomous senior C++/Qt software engineer.

Work directly in the repository.

First inspect the existing code and available build environment. If the repository is empty, initialize the project.

Then:

1. Present a concise technical architecture and implementation plan.
2. Implement Phase 1 completely.
3. Configure CMake and Qt integration.
4. Compile the application.
5. Run automated tests.
6. Fix build errors and failing tests.
7. Validate generated outputs using synthetic input images.
8. Document how to build and run the application.
9. Report measured performance and remaining limitations.

Do not generate large amounts of placeholder code.

Do not claim a feature is complete until it is implemented and verified.

Keep the code production-oriented, understandable and modular.

Avoid unnecessary abstraction, inheritance hierarchies and third-party dependencies.

When a design choice is ambiguous, prefer a simple, testable implementation and document the decision.

**Final acceptance criteria for Phase 1:**

A user can start the application, load a portrait, choose a circular pin count, generate a recognizable string art approximation without blocking the UI, inspect the result, and export both a PNG image and an ordered CSV winding sequence.

Begin implementation with Phase 1.
