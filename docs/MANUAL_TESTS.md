# Manual Test Checklist

The automated suites (`ctest`, 10 suites) cover the engine, file formats and
the main UI workflows headlessly. The items below involve real input devices,
native dialogs, hover states or a real application restart and need a person.
Use `build/examples/make-sample-images samples/` for test pictures, or your
own photos.

## Start and files
- [ ] Start `stringart-studio`; the window opens without console warnings.
- [ ] Toolbar *Open image…*: JPEG, PNG, BMP and WebP files load; a broken file
      shows an error in the status line.
- [ ] Drag an image from the file manager onto the window; it loads.
- [ ] Open / Save project; reopen the file after restarting the app.
- [ ] Each file dialog reopens in the last folder used for that kind of file.

## Image page
- [ ] Brightness / contrast / gamma / invert update the *Processed* view and
      the histogram immediately.
- [ ] Rotation slider and ±90° buttons rotate the *Original* view.
- [ ] Crop frame on *Original*: drag to move, corner handle keeps the aspect,
      frame can extend beyond the photo (white area), *Fit whole photo*,
      *Reset crop*.
- [ ] Hovering a parameter label shows its help tooltip.

## Frame page
- [ ] Circle / Ellipse / Rectangle change the outline on *Processed*.
- [ ] Ellipse / Rectangle: the crop frame's side handles change the aspect
      and the *Height (mm)* field follows.
- [ ] Custom: *Import CSV…* with a pin file (x, y in mm); invalid files show
      an error.
- [ ] *Advanced* section opens and closes.

## Thread page
- [ ] Single colour: palette presets change the result colours without
      re-running.
- [ ] *Multi-colour threads*: presets, colour picker on a swatch, *Add
      colour…* (max 8), remove (min 1), board colour picker.

## Run and results
- [ ] *Start*; the String art and Residual views update live; the UI stays
      responsive while dragging sliders and zooming.
- [ ] Pause / Resume / Cancel; a cancelled run keeps its partial result.
- [ ] *Refine sequence* and *Virtual reconstruction (LS)* (single colour);
      both are disabled for multi-colour results.
- [ ] *Compare* divider on the String art view can be dragged.
- [ ] Zoom with the mouse wheel around the cursor, pan by dragging, *Fit*,
      *Pins* overlay with indices.

## Winding playback
- [ ] Timeline drag, ◀ ▶ buttons, Left / Right keys, Space for play / pause,
      speed selection, *Go to* field, red highlight of the current segment,
      colour dot for multi-colour results.
- [ ] *Export frame…* writes the current frame.

## Export
- [ ] PNG at 1024–8192 px, CSV in mm and px, SVG (open in a browser or
      Inkscape), JSON.

## Persistence
- [ ] Close the app normally, start it again: window size / position (X11;
      Wayland decides positions itself), maximized state, selected parameter
      page and all parameters are restored; crop and rotation are not.
