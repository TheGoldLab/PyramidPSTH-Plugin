#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'EOF'
Usage: build_xcode.sh [/absolute/path/to/OpenEphysGUI] [Debug|Release|RelWithDebInfo|MinSizeRel]

Syncs this standalone PyramidPSTH plugin into an Open Ephys checkout and builds
its Xcode scheme.

Arguments:
  /absolute/path/to/OpenEphysGUI   Optional path to the Open Ephys GUI checkout
  configuration                    Optional Xcode build configuration (default: Release)

Default Open Ephys path resolution (when path is omitted):
  1) PYRAMID_OE_DIR env var
  2) /Users/lowell/Documents/GitHub/neuro-stack/OpenEphysGUI

Environment variables:
  PYRAMID_OE_DIR                   Optional default Open Ephys checkout path
  PYRAMID_SCHEME                   Xcode scheme to build (default: PyramidPSTH)
  PYRAMID_DESTINATION              Optional xcodebuild -destination value
  PYRAMID_OPEN_XCODE=1             Open the generated Xcode project after the build
EOF
}

if [[ ${1:-} == "-h" || ${1:-} == "--help" ]]; then
  usage
  exit 0
fi

if [[ $# -gt 2 ]]; then
  usage >&2
  exit 1
fi

DEFAULT_OE_DIR="${PYRAMID_OE_DIR:-/Users/lowell/Documents/GitHub/neuro-stack/OpenEphysGUI}"
OE_DIR="$DEFAULT_OE_DIR"
CONFIGURATION="Release"

case $# in
  0)
    ;;
  1)
    case "$1" in
      Debug|Release|RelWithDebInfo|MinSizeRel)
        CONFIGURATION="$1"
        ;;
      *)
        OE_DIR="$1"
        ;;
    esac
    ;;
  2)
    OE_DIR="$1"
    CONFIGURATION="$2"
    ;;
esac

echo "==> Using OpenEphysGUI checkout: $OE_DIR"
SCHEME="${PYRAMID_SCHEME:-PyramidPSTH}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
SYNC_SCRIPT="$SCRIPT_DIR/sync_into_open_ephys.sh"
BUILD_DIR="$OE_DIR/Build"
XCODE_PROJECT="$BUILD_DIR/open-ephys-GUI.xcodeproj"

if [[ ! -d "$OE_DIR" ]]; then
  echo "Open Ephys directory does not exist: $OE_DIR" >&2
  exit 1
fi

if [[ ! -d "$OE_DIR/Plugins" ]]; then
  echo "Could not find Plugins folder in: $OE_DIR" >&2
  exit 1
fi

if [[ ! -x "$SYNC_SCRIPT" ]]; then
  echo "Sync script is missing or not executable: $SYNC_SCRIPT" >&2
  exit 1
fi

echo "==> Syncing plugin into OpenEphysGUI"
"$SYNC_SCRIPT" "$OE_DIR"

if [[ ! -d "$BUILD_DIR" ]]; then
  echo "==> Creating build directory"
  mkdir -p "$BUILD_DIR"
fi

if [[ ! -d "$XCODE_PROJECT" ]]; then
  echo "==> Generating Xcode project with CMake"
  (
    cd "$BUILD_DIR"
    cmake -G Xcode ..
  )
fi

BUILD_CMD=(xcodebuild -project "$XCODE_PROJECT" -scheme "$SCHEME" -configuration "$CONFIGURATION" build)

if [[ -n "${PYRAMID_DESTINATION:-}" ]]; then
  BUILD_CMD+=( -destination "$PYRAMID_DESTINATION" )
fi

echo "==> Building scheme '$SCHEME' ($CONFIGURATION)"
"${BUILD_CMD[@]}"

PLUGIN_BUNDLE="$BUILD_DIR/$CONFIGURATION/Open Ephys GUI.app/Contents/PlugIns/PyramidPSTH.bundle"
if [[ -d "$PLUGIN_BUNDLE" ]]; then
  echo "==> Built plugin bundle: $PLUGIN_BUNDLE"
else
  echo "==> Build finished; plugin bundle path not found yet: $PLUGIN_BUNDLE"
fi

if [[ "${PYRAMID_OPEN_XCODE:-0}" == "1" ]]; then
  echo "==> Opening Xcode project"
  open "$XCODE_PROJECT"
fi
