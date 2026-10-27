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

## Frame page (shape and placement together, *Original* view)
- [ ] Circle / Ellipse / Rectangle change the yellow frame on the photo;
      Ellipse / Rectangle side handles change the aspect and *Height (mm)*
      follows.
- [ ] Crop frame: drag to move, corner handle keeps the aspect, frame can
      extend beyond the photo (white area), *Fit whole photo*, *Reset crop*.
- [ ] Rotation slider and ±90° buttons rotate the photo.
- [ ] Custom: *Import CSV…* with a pin file (x, y in mm); invalid files show
      an error.
- [ ] *Advanced* section opens and closes.
- [ ] Hovering a parameter label shows its help tooltip.

## Tone page (*Processed* view)
- [ ] Brightness / contrast / gamma / invert update the *Processed* view and
      the histogram immediately.

## Thread page
- [ ] Single colour: palette presets change the result colours without
      re-running.
- [ ] *Multi-colour threads*: presets, colour picker on a swatch, *Add
      colour…* (max 8), remove (min 1), board colour picker.
- [ ] *Layer order* (Auto, light to dark, dark to light, palette order): the
      winding guide and winding mode show only a few thread changes.
- [ ] *From image … Suggest* on a colourful photo: progress in the button,
      clicking again cancels, the palette is replaced when it finishes.

## Run and results
- [ ] *Effort* Low / Medium / High changes the working resolution shown in
      the hint (384 / 512 / 1024 px); a High run takes visibly longer.
- [ ] *Start*; the String art view (thread and error map) updates live; the UI stays
      responsive while dragging sliders and zooming.
- [ ] Pause / Resume / Cancel; a cancelled run keeps its partial result.
- [ ] *Refine sequence* and *Virtual reconstruction (LS)* (single colour);
      both are disabled for multi-colour results.
- [ ] String art view mode: *Error map* shows red/blue with a legend;
      *Compare* shows the target left of a divider that can be dragged.
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

## Workshop
- [ ] *Pin template…* on A4: print one sheet at 100% and measure the 100 mm
      scale bar; tape two neighbouring sheets along the dashed line and check
      that the round marks coincide; pin numbers are readable.
- [ ] *Pin template…* as one sheet: page size equals the frame plus margins
      in a PDF viewer's document properties.
- [ ] *Winding guide…*: thread lengths match the panel; multi-colour rows
      show colour swatches and shaded thread changes.
- [ ] *Winding mode*: large target pin, next pins, keys and a USB foot pedal
      (Page Down / Page Up), auto-advance, *Speak pins* (needs a speech
      engine, e.g. speech-dispatcher on Linux); Esc returns; reopening the
      project resumes at the same step.

## Undo, presets, runs
- [ ] Drag a slider, then Ctrl+Z: one step back to the value before the drag;
      Ctrl+Shift+Z redoes; arrows in the toolbar follow.
- [ ] *Presets ▾ → Save current parameters as…*, change parameters, pick the
      preset; restart the app: the preset is still listed; delete it.
- [ ] Run twice with different settings, open *Runs*: two runs side by side,
      error difference, click thumbnails to pick other runs, *Use settings*.

## Persistence
- [ ] Close the app normally, start it again: window size / position (X11;
      Wayland decides positions itself), maximized state, selected parameter
      page and all parameters are restored; crop and rotation are not.
