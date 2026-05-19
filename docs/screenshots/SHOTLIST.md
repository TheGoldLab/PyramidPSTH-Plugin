# PyramidPSTH README Screenshot Shot List

Use this checklist to replace placeholder images in `docs/screenshots/` with real captures while keeping filenames unchanged.

## General capture guidelines

- Use PNG when possible.
- Target width: ~1400 to 2000 px (landscape).
- Keep UI text readable at GitHub page width.
- Crop tightly to relevant controls, but keep enough context to orient the reader.
- Prefer a dark/light theme that matches your lab defaults.
- Avoid subject identifiers or sensitive paths in view.

## Required shots

## 1) `01-processor-setup.svg` (replace with `01-processor-setup.png`)

Capture:

- Open Ephys pipeline showing processor placement.
- Include `File Reader` and/or `UDP Events` feeding `Pyramid PSTH`.
- Include selected stream/record node context if visible.

Goal:

- Show where PyramidPSTH sits in the signal chain.

## 2) `02-electrodes-and-display.svg` (replace with `02-electrodes-and-display.png`)

Capture:

- PyramidPSTH editor area with electrode/unit selection controls.
- Raster/PSTH window controls (`pre_ms`, `post_ms`, `bin_size`).

Goal:

- Show basic visualization setup controls.

## 3) `03-trial-and-ttl-lines.svg` (replace with `03-trial-and-ttl-lines.png`)

Capture:

- Trial start TTL line selection.
- Alignment TTL line selection.
- Any relevant stream/line dropdowns.

Goal:

- Show trial-window and TTL alignment source setup.

## 4) `04-rules-and-conditions.svg` (replace with `04-rules-and-conditions.png`)

Capture:

- Rules CSV loaded state.
- Condition rows and optional filter values.
- Condition-related dropdowns/fields populated.

Goal:

- Show how condition matching and filtering are configured.

## 5) `05-event-code-alignment.svg` (replace with `05-event-code-alignment.png`)

Capture:

- Alignment mode switched to event code.
- Alignment condition dropdown/selection.
- `assume synced` option shown in the intended state.

Goal:

- Show event-code alignment setup and synced-stream expectation.

## Replace workflow

1. Capture screenshot.
2. Save with the same basename in `docs/screenshots/`.
3. Update `README.md` image extension from `.svg` to `.png` for that file.
4. Verify rendering in GitHub markdown preview.

