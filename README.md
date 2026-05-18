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

## UDP replay test workflow (live-like)

Two helper scripts are included under `scripts/`:

- `scripts/mute_message_center.py` — backs up and clears saved `MessageCenter` events in a recording (to avoid duplicate text events from `File Reader` + UDP replay).
- `scripts/replay_udp_events.py` — replays Rex-like text payloads to `UDPEvents` over localhost UDP, with optional sync TTL injection from recorded TTL arrays.

### 1) Optional: mute saved MessageCenter events

```bash
python3 scripts/mute_message_center.py \
   --recording-dir "/Volumes/Extreme SSD/Neuropixel/MrM/Synthetic/MrM_NP_2026-02-02_AP_only/Record Node 107/experiment1/recording1"
```

### 2) Run replay while acquisition is active

In Open Ephys, build a chain with `File Reader -> UDP Events -> Pyramid PSTH` on the same selected stream.
Set `UDP Events` host/port to match script args (default `127.0.0.1:12345`).

Quick one-command workflow (recommended):

```bash
bash scripts/start_udp_replay.sh \
   --recording-dir "/Volumes/Extreme SSD/Neuropixel/MrM/Synthetic/MrM_NP_2026-02-02_AP_only/Record Node 107/experiment1/recording1"
```

This wrapper waits for `UDP Events` ACK readiness, so you can keep your usual routine:
1) press **Start Acquisition** in GUI,
2) run one command above.

Then run:

```bash
python3 scripts/replay_udp_events.py \
   --recording-dir "/Volumes/Extreme SSD/Neuropixel/MrM/Synthetic/MrM_NP_2026-02-02_AP_only/Record Node 107/experiment1/recording1" \
   --host 127.0.0.1 --port 12345 \
   --wait-for-udpevents \
   --inject-sync-ttl \
   --ttl-stream-name "Neuropix-PXI-122.ProbeA-AP" \
   --sync-line-1-based 4 \
   --sync-state both \
   --strip-udp-suffix \
   --speed 1.0
```

`--wait-for-udpevents` lets you keep your normal workflow: press **Start Acquisition**, then run the command; the script waits for UDPEvents ACK readiness before replaying.

## CI workflow behavior

`build-plugin-binaries.yml` does this:

1. Checks out this plugin repo.
2. Checks out `open-ephys/plugin-GUI`.
3. Replaces `OpenEphysGUI/Plugins/PyramidPSTH` with this repo’s plugin source.
4. Ensures `add_subdirectory(PyramidPSTH)` exists in `OpenEphysGUI/Plugins/CMakeLists.txt`.
5. Builds target `PyramidPSTH` on:
   - `macos-latest` → uploads `PyramidPSTH-macos.zip`
   - `windows-latest` → uploads `PyramidPSTH-windows.zip`

### Faster Windows-only distribution

If you only need a Windows plugin (and want to avoid full macOS+Windows release time), use:

- `.github/workflows/build-windows-plugin-fast.yml`

This workflow:

1. Builds only on `windows-latest`
2. Uploads `PyramidPSTH-windows.zip` as an Actions artifact (`PyramidPSTH-windows-fast`)
3. Optionally uploads/overwrites `PyramidPSTH-windows.zip` on an existing GitHub Release tag (for example `v1.0.0`)

Recommended usage:

- Keep your standard cross-platform release workflow for official full releases.
- Use the fast Windows-only workflow for quick turnaround when only Windows users need an update.

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
