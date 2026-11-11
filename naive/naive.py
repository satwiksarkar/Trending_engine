#!/usr/bin/env python3
"""Correct, intentionally slow baseline for the C++ trending engine.

The benchmark includes file reading, timestamp parsing, finding the latest
observed timestamp, recounting the complete event list, and sorting all active
pages. It deliberately does NOT use an incremental data structure.

Supported timestamp formats:
  * Unix epoch seconds, e.g. 1794416714
  * UTC ISO-8601, e.g. 2026-11-11T15:14:52.257Z

Run from any directory:
  python naive.py [events.tsv] [window_seconds] [top_k]
"""

from __future__ import annotations

import sys
import time
from collections import Counter
from datetime import datetime, timezone
from pathlib import Path
from typing import Optional


def parse_timestamp(value: str) -> Optional[int]:
    """Parse a Unix-seconds or UTC ISO-8601 timestamp to whole seconds.

    Fractional seconds are intentionally truncated to match engine.cpp.
    """
    value = value.strip()
    if not value:
        return None

    # Fast path for integer Unix timestamps.
    if value.isdigit() or (value.startswith("-") and value[1:].isdigit()):
        try:
            return int(value)
        except ValueError:
            return None

    # The dataset uses UTC timestamps ending in Z. fromisoformat also accepts
    # fractional seconds, matching the C++ parser's whole-second semantics.
    try:
        iso_value = value[:-1] + "+00:00" if value.endswith("Z") else value
        parsed = datetime.fromisoformat(iso_value)
        if parsed.tzinfo is None:
            # Treat timezone-less ISO values as UTC, not machine-local time.
            parsed = parsed.replace(tzinfo=timezone.utc)
        return int(parsed.timestamp())
    except (ValueError, OverflowError, OSError):
        return None


def run_naive(tsv_file: Path, window_seconds: int = 600, top_k: int = 10) -> None:
    if window_seconds <= 0:
        raise ValueError("window_seconds must be positive")
    if top_k < 0:
        raise ValueError("top_k cannot be negative")

    # Start before opening/reading the file. This is important: the C++ engine
    # also times file reading, parsing, aggregation/expiry and Top-K selection.
    total_start = time.perf_counter()
    parse_start = total_start

    events: list[tuple[int, str]] = []
    rows_seen = 0
    rows_skipped = 0

    with tsv_file.open("r", encoding="utf-8", newline="") as source:
        for raw_line in source:
            rows_seen += 1
            line = raw_line.rstrip("\r\n")
            if not line:
                continue

            timestamp_text, separator, page = line.partition("\t")
            if not separator or not page:
                rows_skipped += 1
                continue

            timestamp = parse_timestamp(timestamp_text)
            if timestamp is None:
                rows_skipped += 1
                continue

            events.append((timestamp, page))

    parse_end = time.perf_counter()

    if not events:
        total_end = time.perf_counter()
        print("No valid events found.")
        print(f"Rows seen: {rows_seen} | Rows skipped: {rows_skipped}")
        print(f"Total time: {total_end - total_start:.6f} seconds")
        return

    # Naive baseline: find the final event-time, then scan every stored event.
    recount_start = time.perf_counter()
    latest_timestamp = max(timestamp for timestamp, _ in events)
    cutoff = latest_timestamp - window_seconds

    counts: Counter[str] = Counter()
    for timestamp, page in events:
        if cutoff < timestamp <= latest_timestamp:
            counts[page] += 1
    recount_end = time.perf_counter()

    # Deliberately sort every active page (rather than using heap/nsmallest).
    sort_start = time.perf_counter()
    ranked = sorted(counts.items(), key=lambda item: (-item[1], item[0]))
    top_pages = ranked[:top_k]
    sort_end = time.perf_counter()

    total_end = time.perf_counter()

    print(f"\nNaive Top {top_k} at Unix timestamp {latest_timestamp}:")
    for index, (page, count) in enumerate(top_pages, start=1):
        print(f"{index}. {count} edits\t{page}")

    print("\n--- Naive benchmark ---")
    print(f"Input file: {tsv_file}")
    print(f"Rows seen: {rows_seen}")
    print(f"Valid events: {len(events)}")
    print(f"Rows skipped: {rows_skipped}")
    print(f"Active unique pages: {len(counts)}")
    print(f"Window: {window_seconds} seconds")
    print(f"File read + parse: {parse_end - parse_start:.6f} seconds")
    print(f"Naive full recount: {recount_end - recount_start:.6f} seconds")
    print(f"Full sort + Top-K slice: {sort_end - sort_start:.6f} seconds")
    print(f"Total (read + parse + recount + sort): {total_end - total_start:.6f} seconds")
    print("------------------------")


if __name__ == "__main__":
    project_root = Path(__file__).resolve().parent.parent
    default_file = project_root / "data" / "events.tsv"

    input_file = Path(sys.argv[1]).expanduser() if len(sys.argv) >= 2 else default_file
    window = int(sys.argv[2]) if len(sys.argv) >= 3 else 600
    k = int(sys.argv[3]) if len(sys.argv) >= 4 else 10

    if not input_file.is_file():
        raise SystemExit(f"Error: input file not found: {input_file}")

    run_naive(input_file, window, k)
