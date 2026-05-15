#!/usr/bin/env python3
import argparse
import bisect
import re
import socket
import struct
import sys
import time
from dataclasses import dataclass
from pathlib import Path
from typing import List, Optional, Sequence, Tuple

import numpy as np


SYNC_TEXT_PREFIX = "UDP Events sync on line "
UDP_SUFFIX_RE = re.compile(r"@(-?\d+(?:\.\d+)?)=(-?\d+)\s*$")
SYNC_LINE_RE = re.compile(r"^UDP Events sync on line\s+(\d+)")


@dataclass
class ReplayEvent:
    kind: str  # "ttl" or "text"
    client_secs: float
    ttl_line_0: int = 0
    ttl_state: int = 0
    text: str = ""


def decode_text_array(text_array: np.ndarray) -> List[str]:
    decoded: List[str] = []
    for item in text_array:
        if isinstance(item, bytes):
            decoded.append(item.decode("utf-8", errors="replace"))
        else:
            decoded.append(str(item))
    return decoded


def strip_udp_suffix(message: str) -> str:
    return UDP_SUFFIX_RE.sub("", message)


def parse_udp_suffix(message: str) -> Optional[Tuple[float, int]]:
    match = UDP_SUFFIX_RE.search(message)
    if not match:
        return None
    return float(match.group(1)), int(match.group(2))


def load_message_events(
    recording_dir: Path,
    strip_suffix: bool,
    drop_udp_sync_text: bool,
) -> Tuple[List[ReplayEvent], List[ReplayEvent]]:
    message_dir = recording_dir / "events" / "MessageCenter"
    text_path = message_dir / "text.npy"
    ts_path = message_dir / "timestamps.npy"

    if not text_path.exists() or not ts_path.exists():
        raise FileNotFoundError(f"Missing MessageCenter files under: {message_dir}")

    text_array = np.load(text_path, allow_pickle=True)
    ts_array = np.load(ts_path, allow_pickle=True)

    if text_array.shape[0] == 0:
        backups = sorted(message_dir.glob("_backup_udp_replay_*"), key=lambda p: p.name)
        if backups:
            backup_dir = backups[-1]
            backup_text = backup_dir / "text.npy"
            backup_ts = backup_dir / "timestamps.npy"
            if backup_text.exists() and backup_ts.exists():
                text_array = np.load(backup_text, allow_pickle=True)
                ts_array = np.load(backup_ts, allow_pickle=True)
                print(
                    "MessageCenter active arrays are empty; "
                    f"using backup events from {backup_dir}"
                )

    if text_array.shape[0] != ts_array.shape[0]:
        raise RuntimeError(
            f"MessageCenter shape mismatch text={text_array.shape} timestamps={ts_array.shape}"
        )

    decoded = decode_text_array(text_array)
    events: List[ReplayEvent] = []
    sync_events: List[ReplayEvent] = []
    seen_sync_keys = set()

    for text, ts in zip(decoded, ts_array):
        suffix = parse_udp_suffix(text)
        event_client_secs = float(ts)
        if suffix is not None:
            event_client_secs = suffix[0]

        payload = strip_udp_suffix(text) if strip_suffix else text

        if payload.startswith(SYNC_TEXT_PREFIX):
            line_match = SYNC_LINE_RE.match(payload)
            if line_match is not None and suffix is not None:
                line_1 = int(line_match.group(1))
                line_0 = max(0, line_1 - 1)
                sync_key = (line_0, round(event_client_secs, 9))
                if sync_key not in seen_sync_keys:
                    seen_sync_keys.add(sync_key)
                    sync_events.append(
                        ReplayEvent(
                            kind="ttl",
                            client_secs=event_client_secs,
                            ttl_line_0=line_0,
                            ttl_state=1,
                        )
                    )

            if drop_udp_sync_text:
                continue

        payload = payload.strip()
        if not payload:
            continue

        events.append(ReplayEvent(kind="text", client_secs=event_client_secs, text=payload))

    return events, sync_events


def state_matches(state_value: int, wanted: str) -> bool:
    if wanted == "both":
        return True
    if wanted == "high":
        return state_value > 0
    if wanted == "low":
        return state_value < 0
    return False


def load_sync_ttl_events(
    recording_dir: Path,
    ttl_stream_name: str,
    sync_line_1: int,
    sync_state: str,
) -> List[ReplayEvent]:
    ttl_dir = recording_dir / "events" / ttl_stream_name / "TTL"
    ts_path = ttl_dir / "timestamps.npy"
    states_path = ttl_dir / "states.npy"

    if not ts_path.exists() or not states_path.exists():
        raise FileNotFoundError(f"Missing TTL files under: {ttl_dir}")

    ts_array = np.load(ts_path, allow_pickle=True)
    states_array = np.load(states_path, allow_pickle=True)

    if ts_array.shape[0] != states_array.shape[0]:
        raise RuntimeError(
            f"TTL shape mismatch timestamps={ts_array.shape} states={states_array.shape}"
        )

    events: List[ReplayEvent] = []
    sync_line_0 = sync_line_1 - 1

    for ts, raw_state in zip(ts_array, states_array):
        state_value = int(raw_state)
        line_1 = abs(state_value)
        if line_1 != sync_line_1:
            continue
        if not state_matches(state_value, sync_state):
            continue

        events.append(
            ReplayEvent(
                kind="ttl",
                client_secs=float(ts),
                ttl_line_0=sync_line_0,
                ttl_state=1 if state_value > 0 else 0,
            )
        )

    return events


def build_ttl_packet(client_secs: float, line_0: int, state: int) -> bytearray:
    packet = bytearray(11)
    struct.pack_into("B", packet, 0, 1)
    struct.pack_into("d", packet, 1, float(client_secs))
    struct.pack_into("B", packet, 9, int(line_0) & 0xFF)
    struct.pack_into("B", packet, 10, 1 if state else 0)
    return packet


def build_text_packet(client_secs: float, text: str) -> bytearray:
    text_bytes = text.encode("utf-8")
    packet = bytearray(11 + len(text_bytes))
    struct.pack_into("B", packet, 0, 2)
    struct.pack_into("d", packet, 1, float(client_secs))
    struct.pack_into("H", packet, 9, socket.htons(len(text_bytes)))
    packet[11 : 11 + len(text_bytes)] = text_bytes
    return packet


def probe_udp_events_ready(
    udp_socket: socket.socket,
    destination: Tuple[str, int],
    timeout_seconds: float,
    retry_interval_seconds: float,
) -> None:
    deadline = time.time() + timeout_seconds
    probe = bytes([255])

    while True:
        udp_socket.sendto(probe, destination)
        try:
            data, _ = udp_socket.recvfrom(64)
            if len(data) >= 8:
                return
        except socket.timeout:
            pass

        if time.time() >= deadline:
            raise TimeoutError(
                f"Timed out waiting for UDPEvents ACK at {destination[0]}:{destination[1]}"
            )
        time.sleep(retry_interval_seconds)


def replay_events(
    events: Sequence[ReplayEvent],
    udp_socket: socket.socket,
    destination: Tuple[str, int],
    speed: float,
    max_events: int,
    start_delay_seconds: float,
    wait_ack_each: bool,
    log_every: int,
    dry_run: bool,
) -> None:
    if not events:
        print("No replay events to send.")
        return

    sorted_events = sorted(
        events,
        key=lambda event: (event.client_secs, 0 if event.kind == "ttl" else 1),
    )
    if max_events > 0:
        sorted_events = sorted_events[:max_events]

    if not sorted_events:
        print("No replay events after filtering.")
        return

    base_secs = sorted_events[0].client_secs

    start_mono = time.monotonic() + max(0.0, start_delay_seconds)
    if start_delay_seconds > 0:
        time.sleep(start_delay_seconds)

    sent = 0
    text_sent = 0
    ttl_sent = 0

    for event in sorted_events:
        event_offset = (event.client_secs - base_secs) / speed
        target_time = start_mono + max(0.0, event_offset)

        sleep_for = target_time - time.monotonic()
        if sleep_for > 0:
            time.sleep(sleep_for)

        if event.kind == "ttl":
            packet = build_ttl_packet(event.client_secs, event.ttl_line_0, event.ttl_state)
            ttl_sent += 1
        else:
            packet = build_text_packet(event.client_secs, event.text)
            text_sent += 1

        if not dry_run:
            udp_socket.sendto(packet, destination)
            if wait_ack_each:
                try:
                    udp_socket.recvfrom(256)
                except socket.timeout:
                    pass

        sent += 1
        if log_every > 0 and sent % log_every == 0:
            print(f"Sent {sent}/{len(sorted_events)} events (text={text_sent}, ttl={ttl_sent})")

    print(f"Replay done. sent={sent}, text={text_sent}, ttl={ttl_sent}, dry_run={dry_run}")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Replay Rex-like UDP text events into UDPEvents using a saved Open Ephys recording."
    )
    parser.add_argument(
        "--recording-dir",
        required=True,
        help="Path to recording folder containing structure.oebin and events/",
    )
    parser.add_argument("--host", default="127.0.0.1", help="UDPEvents host")
    parser.add_argument("--port", type=int, default=12345, help="UDPEvents port")
    parser.add_argument(
        "--bind-host", default="127.0.0.1", help="Local UDP bind host for ACK replies"
    )
    parser.add_argument(
        "--bind-port",
        type=int,
        default=0,
        help="Local UDP bind port for ACK replies (0 = ephemeral)",
    )
    parser.add_argument(
        "--speed",
        type=float,
        default=1.0,
        help="Replay speed factor relative to recorded timestamps",
    )
    parser.add_argument(
        "--max-events",
        type=int,
        default=0,
        help="Replay at most this many events after sorting by client time (0 = all)",
    )
    parser.add_argument(
        "--start-delay-seconds",
        type=float,
        default=0.0,
        help="Delay before first replay event is sent",
    )
    parser.add_argument(
        "--strip-udp-suffix",
        action="store_true",
        help="Strip trailing @soft_secs=sample suffix from saved MessageCenter text",
    )
    parser.add_argument(
        "--keep-udp-sync-text",
        action="store_true",
        help="Keep MessageCenter texts that start with 'UDP Events sync on line ...'",
    )
    parser.add_argument(
        "--inject-sync-ttl",
        action="store_true",
        help="Inject soft TTL sync events from recording TTL arrays so UDPEvents can build sync estimates.",
    )
    parser.add_argument(
        "--ttl-stream-name",
        default="Neuropix-PXI-122.ProbeA-AP",
        help="TTL event stream folder name under events/ used when --inject-sync-ttl is enabled",
    )
    parser.add_argument(
        "--sync-line-1-based",
        type=int,
        default=4,
        help="1-based TTL sync line to replay as soft TTL to UDPEvents",
    )
    parser.add_argument(
        "--sync-state",
        choices=["both", "high", "low"],
        default="both",
        help="TTL sync state filter when --inject-sync-ttl is enabled",
    )
    parser.add_argument(
        "--min-sync-markers-before-text",
        type=int,
        default=4,
        help=(
            "When using --inject-sync-ttl, drop text events before this many unique "
            "sync-marker times have occurred (default: 4)."
        ),
    )
    parser.add_argument(
        "--max-secs-since-sync-for-text",
        type=float,
        default=2.0,
        help=(
            "When using --inject-sync-ttl, drop text events if the most recent replayed "
            "sync TTL is older than this many seconds (default: 2.0)."
        ),
    )
    parser.add_argument(
        "--wait-for-udpevents",
        action="store_true",
        help="Wait until UDPEvents ACKs probe packets before replay starts",
    )
    parser.add_argument(
        "--wait-timeout-seconds",
        type=float,
        default=120.0,
        help="Timeout while waiting for UDPEvents readiness",
    )
    parser.add_argument(
        "--wait-retry-seconds",
        type=float,
        default=0.5,
        help="Retry interval while waiting for UDPEvents readiness",
    )
    parser.add_argument(
        "--wait-ack-each",
        action="store_true",
        help="Wait for ACK after each replay packet",
    )
    parser.add_argument(
        "--socket-timeout-seconds",
        type=float,
        default=0.25,
        help="UDP receive timeout for ACK reads",
    )
    parser.add_argument(
        "--log-every",
        type=int,
        default=100,
        help="Progress print cadence in number of sent events (0 disables)",
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="Load and schedule events but do not send UDP packets",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()

    if args.speed <= 0:
        print("--speed must be > 0", file=sys.stderr)
        return 2

    recording_dir = Path(args.recording_dir).expanduser().resolve()
    if not recording_dir.exists():
        print(f"Recording dir does not exist: {recording_dir}", file=sys.stderr)
        return 2

    message_events, sync_marker_events = load_message_events(
        recording_dir=recording_dir,
        strip_suffix=args.strip_udp_suffix,
        drop_udp_sync_text=not args.keep_udp_sync_text,
    )

    ttl_events: List[ReplayEvent] = []
    if args.inject_sync_ttl:
        if sync_marker_events:
            ttl_events = sync_marker_events
            print(
                "Using sync TTL events parsed from MessageCenter UDP sync markers: "
                f"count={len(ttl_events)}"
            )
        else:
            ttl_events = load_sync_ttl_events(
                recording_dir=recording_dir,
                ttl_stream_name=args.ttl_stream_name,
                sync_line_1=args.sync_line_1_based,
                sync_state=args.sync_state,
            )

    if args.inject_sync_ttl and message_events and ttl_events:
        earliest_ttl_secs = min(event.client_secs for event in ttl_events)
        before_count = len(message_events)
        message_events = [event for event in message_events if event.client_secs >= earliest_ttl_secs]
        dropped = before_count - len(message_events)
        if dropped > 0:
            print(
                "Dropped pre-sync text events to avoid unsynced UDPEvents text replay: "
                f"dropped={dropped}, first_sync_ttl={earliest_ttl_secs:.6f}"
            )

        unique_sync_secs = sorted({round(event.client_secs, 9) for event in ttl_events})
        warmup_markers = max(1, args.min_sync_markers_before_text)
        if len(unique_sync_secs) >= warmup_markers:
            warmup_sync_secs = unique_sync_secs[warmup_markers - 1]
            before_warmup_count = len(message_events)
            message_events = [
                event for event in message_events if event.client_secs >= warmup_sync_secs
            ]
            dropped_before_warmup = before_warmup_count - len(message_events)
            if dropped_before_warmup > 0:
                print(
                    "Dropped text events before sync warm-up marker to allow UDPEvents "
                    f"to build sync estimate: dropped={dropped_before_warmup}, "
                    f"warmup_marker_index={warmup_markers}, "
                    f"warmup_sync_ttl={warmup_sync_secs:.6f}"
                )

        ttl_times = sorted({event.client_secs for event in ttl_events})
        before_sync_age_filter = len(message_events)
        kept_text_events: List[ReplayEvent] = []

        for event in message_events:
            ttl_index = bisect.bisect_right(ttl_times, event.client_secs) - 1
            if ttl_index < 0:
                continue
            most_recent_ttl = ttl_times[ttl_index]
            if (event.client_secs - most_recent_ttl) <= args.max_secs_since_sync_for_text:
                kept_text_events.append(event)

        message_events = kept_text_events
        dropped_by_sync_age = before_sync_age_filter - len(message_events)
        if dropped_by_sync_age > 0:
            print(
                "Dropped text events too far from most recent sync TTL: "
                f"dropped={dropped_by_sync_age}, "
                f"max_secs_since_sync_for_text={args.max_secs_since_sync_for_text:.3f}"
            )

    all_events = message_events + ttl_events

    print(
        f"Loaded events: text={len(message_events)} ttl={len(ttl_events)} total={len(all_events)} "
        f"recording={recording_dir}"
    )

    destination = (args.host, args.port)

    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as udp_socket:
        udp_socket.settimeout(args.socket_timeout_seconds)
        udp_socket.bind((args.bind_host, args.bind_port))

        local_bind = udp_socket.getsockname()
        print(f"UDP local bind: {local_bind[0]}:{local_bind[1]} -> destination {destination[0]}:{destination[1]}")

        if args.wait_for_udpevents and not args.dry_run:
            print("Waiting for UDPEvents ACK readiness...")
            probe_udp_events_ready(
                udp_socket=udp_socket,
                destination=destination,
                timeout_seconds=args.wait_timeout_seconds,
                retry_interval_seconds=args.wait_retry_seconds,
            )
            print("UDPEvents ACK detected; starting replay.")

        replay_events(
            events=all_events,
            udp_socket=udp_socket,
            destination=destination,
            speed=args.speed,
            max_events=args.max_events,
            start_delay_seconds=args.start_delay_seconds,
            wait_ack_each=args.wait_ack_each,
            log_every=args.log_every,
            dry_run=args.dry_run,
        )

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
