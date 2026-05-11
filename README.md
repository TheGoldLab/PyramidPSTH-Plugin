# PyramidPSTH standalone plugin repo seed

This folder is a clean starting point for creating a dedicated GitHub repo for the `PyramidPSTH` Open Ephys plugin.

## What is included

- `Plugins/PyramidPSTH/` — plugin source code
- `.github/workflows/build-plugin-binaries.yml` — CI to build plugin artifacts on macOS + Windows
- `.github/workflows/release-plugin-binaries.yml` — tag-triggered workflow that publishes release assets
- `scripts/sync_into_open_ephys.sh` — helper to inject this plugin into a local `OpenEphysGUI` checkout
- `STUDENT_INSTALL.md` — no-command-line install instructions for students

## Create a new GitHub repo from this seed

1. Create a new empty GitHub repo (example: `your-org/PyramidPSTH-plugin`).
2. Copy this folder contents into that repo root.
3. Commit and push.

## Local dev workflow

1. Keep this plugin repo as your source of truth.
2. Sync into an Open Ephys checkout:

```bash
bash scripts/sync_into_open_ephys.sh /absolute/path/to/OpenEphysGUI
```

3. Build from the Open Ephys repo (example macOS):

```bash
cd /absolute/path/to/OpenEphysGUI/Build
cmake -G "Xcode" ..
cmake --build . --config Release --target PyramidPSTH
```

## CI workflow behavior

`build-plugin-binaries.yml` does this:

1. Checks out this plugin repo.
2. Checks out `open-ephys/plugin-GUI`.
3. Replaces `OpenEphysGUI/Plugins/PyramidPSTH` with this repo’s plugin source.
4. Ensures `add_subdirectory(PyramidPSTH)` exists in `OpenEphysGUI/Plugins/CMakeLists.txt`.
5. Builds target `PyramidPSTH` on:
   - `macos-latest` → uploads `PyramidPSTH-macos.zip`
   - `windows-latest` → uploads `PyramidPSTH-windows.zip`

## Recommended branching/release setup

- `main`: stable plugin
- feature branches: active development
- optional tags (`vX.Y.Z`) for release snapshots

You can later add a release workflow that attaches built zip artifacts to GitHub Releases.

## Student-friendly distribution (recommended)

Use GitHub Releases so students only click download files:

1. Create a tag like `v1.0.0`.
2. Push the tag.
3. Workflow `.github/workflows/release-plugin-binaries.yml` builds macOS + Windows plugin binaries.
4. The workflow creates a GitHub Release and uploads:
   - `PyramidPSTH-mac.zip`
   - `PyramidPSTH-windows.zip`

Students then follow `STUDENT_INSTALL.md`.

## Notes

- The plugin depends on Open Ephys GUI internals and is not intended to build fully standalone without checking out Open Ephys GUI source.
- If Open Ephys build internals change upstream, update the CI workflow checkout/build steps accordingly.
