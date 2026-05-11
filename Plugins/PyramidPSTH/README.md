# Pyramid PSTH (scaffold)

This plugin scaffold is a fork-target for a TTL-aligned PSTH workflow with Pyramid-like text event rule evaluation.

## Current capabilities

- Listens for all text events through `handleBroadcastMessage(...)`
- Tracks trial windows from a dedicated trial TTL line (high=start, low=end)
- Counts a matched trial when the selected condition appears within that trial window
- Supports a configurable pre-trial buffer (ms) so condition events just before trial start can still count
- Tracks a separate alignment TTL line for future PSTH alignment/debugging
- Supports alignment source selection:
	- TTL trigger line alignment (existing behavior)
	- Event-code alignment chosen from loaded rules CSV conditions (via editor button)
- In event-code alignment mode, there is no fallback: if the selected alignment event is missing/invalid for a trial, that trial is omitted from matched PSTH/raster accumulation
- Reports live lag statistics in ms (`lag_last_ms`, `lag_mean_ms`, `lag_std_ms`, `lag_suggest_ms`) for the selected condition around trial start

## Lag tracking details

- Lag is computed as `trial_start_ttl_time - nearest_matching_condition_event_time`.
- Positive lag means the condition message arrived before trial start TTL.
- Statistics are computed online with running moments (constant memory), so no per-trial history is stored.
- Event search uses the existing bounded `recentEvents` buffer in the rule engine.
- Includes editor buttons to load one or more CSV rule files
- Uses fail-safe handling so parser/evaluator errors do not stop acquisition

## CSV format (initial)

Header row:

`condition_name,code_key,code_type,operator,expected_value,lookback_ms`

Example:

`Condition 1,trial_id,id,==,42,2000`

`Condition 1,go_cue,time,exists,,1500`

## Notes

This is an initial integration scaffold. Next steps are:

1. Add PSTH visualizer wiring from `online-psth`
2. Add enhancer transforms and richer operators
3. Add per-stream/per-condition rule scoping

## Cross-platform binaries (macOS + Windows)

This repository now includes a dedicated GitHub Actions workflow:

- `.github/workflows/pyramid-psth-plugin.yml`

It builds only the `PyramidPSTH` plugin target on:

- `macos-latest` (artifact contains `PyramidPSTH.bundle` zipped)
- `windows-latest` (artifact contains `PyramidPSTH.dll` zipped)

### Running the workflow

1. Open the **Actions** tab in GitHub.
2. Select **PyramidPSTH Plugin Binaries**.
3. Click **Run workflow**.
4. Download artifacts named:
	- `PyramidPSTH-macos`
	- `PyramidPSTH-windows`

### Installing built plugin binaries

- On macOS: copy `PyramidPSTH.bundle` into Open Ephys GUI plugins folder.
- On Windows: copy `PyramidPSTH.dll` into Open Ephys GUI plugins folder.

For packaged GUI app builds, this is typically the `PlugIns` directory next to or inside the app installation layout.
