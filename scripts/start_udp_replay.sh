#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'EOF'
Usage:
  start_udp_replay.sh --recording-dir /path/to/recording [-- extra replay_udp_events.py args]

Starts local UDP replay with acquisition-friendly defaults:
- waits for UDPEvents ACK readiness
- preserves UDPEvents suffixes from saved text (needed for sample-domain event alignment)
- injects sync TTL from a selected TTL stream/line

Environment overrides:
  PYRAMID_REPLAY_HOST            (default: 127.0.0.1)
  PYRAMID_REPLAY_PORT            (default: 12345)
  PYRAMID_REPLAY_TTL_STREAM      (default: Neuropix-PXI-122.ProbeA-AP)
  PYRAMID_REPLAY_SYNC_LINE_1     (default: 4)
  PYRAMID_REPLAY_SYNC_STATE      (default: both)
  PYRAMID_REPLAY_SPEED           (default: 1.0)
  PYRAMID_REPLAY_LOG_EVERY       (default: 500)
  PYRAMID_REPLAY_STRIP_SUFFIX    (default: 0; set to 1 to strip saved @softSecs=sample suffix)
  PYRAMID_REPLAY_KILL_OTHERS     (default: 1; kill older replay processes first)

Examples:
  bash scripts/start_udp_replay.sh \
    --recording-dir "/Volumes/.../Record Node 107/experiment1/recording1"

  bash scripts/start_udp_replay.sh \
    --recording-dir "/Volumes/.../recording1" \
    -- --max-events 1000 --speed 2.0
EOF
}

if [[ ${1:-} == "-h" || ${1:-} == "--help" ]]; then
  usage
  exit 0
fi

RECORDING_DIR=""
EXTRA_ARGS=()

while [[ $# -gt 0 ]]; do
  case "$1" in
    --recording-dir)
      RECORDING_DIR="${2:-}"
      shift 2
      ;;
    --)
      shift
      EXTRA_ARGS=("$@")
      break
      ;;
    *)
      echo "Unknown argument: $1" >&2
      usage >&2
      exit 1
      ;;
  esac
done

if [[ -z "$RECORDING_DIR" ]]; then
  echo "Missing --recording-dir" >&2
  usage >&2
  exit 1
fi

if [[ ! -d "$RECORDING_DIR" ]]; then
  echo "Recording directory does not exist: $RECORDING_DIR" >&2
  exit 1
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPLAY_SCRIPT="$SCRIPT_DIR/replay_udp_events.py"

if [[ ! -f "$REPLAY_SCRIPT" ]]; then
  echo "Replay script not found: $REPLAY_SCRIPT" >&2
  exit 1
fi

HOST="${PYRAMID_REPLAY_HOST:-127.0.0.1}"
PORT="${PYRAMID_REPLAY_PORT:-12345}"
TTL_STREAM="${PYRAMID_REPLAY_TTL_STREAM:-Neuropix-PXI-122.ProbeA-AP}"
SYNC_LINE_1="${PYRAMID_REPLAY_SYNC_LINE_1:-4}"
SYNC_STATE="${PYRAMID_REPLAY_SYNC_STATE:-both}"
SPEED="${PYRAMID_REPLAY_SPEED:-1.0}"
LOG_EVERY="${PYRAMID_REPLAY_LOG_EVERY:-500}"
STRIP_SUFFIX="${PYRAMID_REPLAY_STRIP_SUFFIX:-0}"
KILL_OTHERS="${PYRAMID_REPLAY_KILL_OTHERS:-1}"

if [[ "$KILL_OTHERS" != "0" ]]; then
  echo "==> Stopping any older replay processes to avoid mixed streams..."
  pkill -f "$REPLAY_SCRIPT" >/dev/null 2>&1 || true
fi

echo "==> Starting UDP replay"
echo "==> Recording: $RECORDING_DIR"
echo "==> Destination: ${HOST}:${PORT}"
echo "==> TTL stream/line/state: ${TTL_STREAM} / ${SYNC_LINE_1} / ${SYNC_STATE}"

echo "==> Waiting for UDPEvents (start acquisition in GUI first if needed)..."

replay_cmd=(
  python3 "$REPLAY_SCRIPT"
  --recording-dir "$RECORDING_DIR"
  --host "$HOST"
  --port "$PORT"
  --wait-for-udpevents
  --inject-sync-ttl
  --ttl-stream-name "$TTL_STREAM"
  --sync-line-1-based "$SYNC_LINE_1"
  --sync-state "$SYNC_STATE"
  --speed "$SPEED"
  --log-every "$LOG_EVERY"
)

if [[ "$STRIP_SUFFIX" == "1" ]]; then
  replay_cmd+=(--strip-udp-suffix)
fi

if [[ ${#EXTRA_ARGS[@]} -gt 0 ]]; then
  replay_cmd+=("${EXTRA_ARGS[@]}")
fi

"${replay_cmd[@]}"
