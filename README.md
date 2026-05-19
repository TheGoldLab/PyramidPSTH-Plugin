# PyramidPSTH Plugin for Open Ephys

`PyramidPSTH` is an Open Ephys plugin for trial-aligned neural visualization using raster and PSTH views, with optional event-code based trial matching and alignment.

## Purpose

This plugin is built for experiments where trial structure and condition labels are carried by text events (for example Rex/Pyramid-style event codes), while spikes and TTL events come from Open Ephys streams.

It acts as a lightweight version of our Pyramid conversion pipeline: [Pyramid](https://github.com/lwthompson2/pyramid).

It lets you:

- Align spikes to a trial event (TTL or event code)
- Match trials by condition rules
- Optionally filter trials with condition-specific filters
- Visualize data per electrode/unit in raster and PSTH panels

## Repository Contents

- `Plugins/PyramidPSTH/` — plugin source code
- `rules/default_ecode_rules.csv` — default event-code definitions you can load directly in the plugin
- `scripts/` — local build/sync and replay helpers
- `STUDENT_INSTALL.md` — no-command-line student install guide
- `.github/workflows/` — CI workflows for plugin binaries

## Screenshots

> The files below are currently placeholders and can be replaced with real screenshots using the same filenames.

### 1) Basic processor setup

![Basic processor setup](docs/screenshots/01-processor-setup.svg)

### 2) Electrode and display window settings

![Electrodes and display windows](docs/screenshots/02-electrodes-and-display.svg)

### 3) Trial start + alignment TTL selection

![Trial and TTL lines](docs/screenshots/03-trial-and-ttl-lines.svg)

### 4) Rules, conditions, and optional filters

![Rules and conditions](docs/screenshots/04-rules-and-conditions.svg)

### 5) Event-code alignment with synced streams

![Event-code alignment](docs/screenshots/05-event-code-alignment.svg)

## Installation

### macOS

1. Download `PyramidPSTH-mac.zip` from the latest GitHub Release.
2. Unzip to get `PyramidPSTH.bundle`.
3. Copy `PyramidPSTH.bundle` into your Open Ephys plugin folder (`PlugIns`).
4. Restart Open Ephys.
5. Add `Pyramid PSTH` from the processor list.

If macOS blocks the plugin, right-click once and choose **Open**.

### Windows

1. Download `PyramidPSTH-windows.zip` from the latest GitHub Release.
2. Extract to get `PyramidPSTH.dll`.
3. Copy `PyramidPSTH.dll` into your Open Ephys plugins folder.
4. Restart Open Ephys.
5. Add `Pyramid PSTH` from the processor list.

Common plugin folders:

- `C:/Users/<username>/AppData/Local/Open Ephys/plugins-api10`
- `C:/ProgramData/OpenEphys/plugins-api8`

For a student-friendly walkthrough, see `STUDENT_INSTALL.md`.

## How to Use

### Basics

1. Add `Pyramid PSTH` to your processing chain.
2. Select electrodes/units you want to visualize.
3. Set raster/PSTH display windows (`pre_ms`, `post_ms`, `bin_size`).
4. Select a **trial start TTL line**.
5. Select an **alignment TTL line** (for TTL alignment mode).

If you intend to align by event code (not TTL), click the align-event-code control, select an alignment condition, and ensure the synced-stream assumption is set appropriately (`assume synced`) before testing alignment.

### Event-code options

### Load a rules CSV

1. Click the rules load button in the plugin editor.
2. Load `rules/default_ecode_rules.csv` (included in this repo), or your own compatible CSV.

Supported rule formats include:

- rule table: `condition_name,code_key,code_type,operator,expected_value,lookback_ms`
- ecode definition table: `type,value,name,...`

### Add conditions and optional filters

1. Add condition rows in the plugin editor.
2. For each condition, specify event code key and optional expected value.
3. Add optional filter values if you want per-condition trial filtering.

### Change alignment to use an event code

1. Switch alignment mode to `event_code`.
2. Select the desired alignment condition (for example `dot_dir` or `trial_start`).
3. Confirm the condition exists in loaded rules.
4. For acquisition-clock alignment behavior, use the synced-stream option appropriately.

## Making Modifications and Building

The plugin is developed in this repo, then synced into an Open Ephys GUI checkout for compilation.

### macOS local build

```bash
bash /Users/lowell/Documents/GitHub/PyramidPSTH-Plugin/scripts/build_xcode.sh
```

This script:

1. syncs `Plugins/PyramidPSTH` into your Open Ephys checkout
2. builds scheme `PyramidPSTH`
3. outputs a `.bundle` in the Open Ephys build products

### Generic sync helper

```bash
bash scripts/sync_into_open_ephys.sh /absolute/path/to/OpenEphysGUI
```

Then build from the Open Ephys side using your preferred generator/toolchain.

## CI Builds (GitHub Actions)

- `.github/workflows/build-plugin-binaries.yml` — macOS + Windows plugin artifacts
- `.github/workflows/build-windows-plugin-fast.yml` — faster Windows-only artifact workflow
- `.github/workflows/release-plugin-binaries.yml` — release-tag publishing workflow

## Notes

- `PyramidPSTH` depends on Open Ephys GUI internals and is not intended as a standalone binary project.
- If Open Ephys build internals change upstream, workflow and local build steps may need updates.
