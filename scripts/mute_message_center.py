#!/usr/bin/env python3
import argparse
import datetime as dt
from pathlib import Path
from typing import Dict

import numpy as np


TARGET_FILES = ("text.npy", "timestamps.npy", "sample_numbers.npy")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Backup and mute MessageCenter events in an Open Ephys recording folder."
    )
    parser.add_argument(
        "--recording-dir",
        required=True,
        help="Path to recording folder containing events/MessageCenter",
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="Print actions without writing files",
    )
    return parser.parse_args()


def load_arrays(message_center_dir: Path) -> Dict[str, np.ndarray]:
    arrays: Dict[str, np.ndarray] = {}
    for name in TARGET_FILES:
        path = message_center_dir / name
        if not path.exists():
            raise FileNotFoundError(f"Missing MessageCenter file: {path}")
        arrays[name] = np.load(path, allow_pickle=True)
    return arrays


def main() -> int:
    args = parse_args()
    recording_dir = Path(args.recording_dir).expanduser().resolve()
    message_center_dir = recording_dir / "events" / "MessageCenter"

    if not message_center_dir.exists():
        raise FileNotFoundError(f"MessageCenter directory not found: {message_center_dir}")

    arrays = load_arrays(message_center_dir)

    print(f"Recording: {recording_dir}")
    for name, array in arrays.items():
        print(f"  {name}: dtype={array.dtype}, shape={array.shape}")

    stamp = dt.datetime.now().strftime("%Y%m%d_%H%M%S")
    backup_dir = message_center_dir / f"_backup_udp_replay_{stamp}"

    print(f"Backup dir: {backup_dir}")

    if args.dry_run:
        print("Dry run only. No files written.")
        return 0

    backup_dir.mkdir(parents=True, exist_ok=False)

    for name, array in arrays.items():
        src = message_center_dir / name
        backup = backup_dir / name
        src.replace(backup)

        empty = np.empty((0,), dtype=array.dtype)
        np.save(src, empty)

    print("MessageCenter muted.")
    print("Original files moved to backup directory above.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
