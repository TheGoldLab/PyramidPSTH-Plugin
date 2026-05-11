#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 1 ]]; then
  echo "Usage: $0 /absolute/path/to/OpenEphysGUI"
  exit 1
fi

OE_DIR="$1"
SEED_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

if [[ ! -d "$OE_DIR/Plugins" ]]; then
  echo "Could not find Plugins folder in: $OE_DIR"
  exit 1
fi

rm -rf "$OE_DIR/Plugins/PyramidPSTH"
cp -R "$SEED_ROOT/Plugins/PyramidPSTH" "$OE_DIR/Plugins/"

if ! grep -q "add_subdirectory(PyramidPSTH)" "$OE_DIR/Plugins/CMakeLists.txt"; then
  echo "add_subdirectory(PyramidPSTH)" >> "$OE_DIR/Plugins/CMakeLists.txt"
  echo "Added add_subdirectory(PyramidPSTH) to Plugins/CMakeLists.txt"
fi

echo "PyramidPSTH synced into: $OE_DIR"
