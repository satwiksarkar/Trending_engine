# Real-Time Trending Engine (C++)

A small streaming analytics project that tracks the most frequently edited Wikimedia pages inside a configurable sliding time window. The project captures Wikimedia's public **RecentChanges** event stream, stores a compact event log, and compares an incremental C++ implementation with an intentionally straightforward Python baseline.

## What it does

- Consumes Wikimedia RecentChanges events over Server-Sent Events (SSE).
- Keeps `edit` and `new` events and creates a page key in the form `wiki|title`.
- Stores both the original event payload (JSONL) and a compact timestamp/page record (TSV).
- Counts edits in a configurable sliding window (600 seconds by default).
- Returns the Top-K pages, ordered by edit count descending and page key ascending for deterministic ties.
- Provides a naive Python implementation as a correctness/performance baseline.

This is a replayable prototype: the recorder captures a live feed, while the C++ and Python programs currently process the saved TSV file. It is not yet a continuously running query service.

## Architecture

![System flowchart](trending_engine_flowchart.png)

### Data format

Each line in `data/events.tsv` has two tab-separated fields:

```text
<timestamp>\t<wiki>|<page title>
```

Example:

```text
1794425555\twikidatawiki|Q142116167
```

The timestamp may be Unix epoch seconds or a UTC ISO-8601 timestamp such as `2026-11-11T15:14:52.257Z`. Fractional seconds are truncated to whole seconds by the current implementations.

The page key combines the wiki project and title so that identically named pages on different Wikimedia projects are counted separately. Tabs and newlines in titles are sanitized by the recorder to preserve the TSV structure.

- `events.jsonl`: original JSON event payloads, useful when the full event metadata is needed.
- `events.tsv`: compact input used by the current trending engines.

The recorder appends to these files. Stop it with `Ctrl+C` when you have collected enough data.

## Repository layout

```text
Trending_engine/
├── capture/
│   └── recorder.py       # Wikimedia SSE recorder
├── data/
│   ├── events.jsonl      # captured raw events (not committed)
│   └── events.tsv        # compact replay dataset (not committed)
├── engine/
│   └── engine.cpp        # C++ sliding-window implementation
├── naive/
│   └── naive.py          # intentionally naive Python baseline
├── tests/                # experiments and test programs
├── bench/                # benchmark outputs/scripts
├── requirements.txt
└── README.md
```

Keep generated data, virtual environments, executables, and benchmark output out of version control. Do not commit `.venv/`, `data/events.jsonl`, or `data/events.tsv`.

## Requirements

- C++17 compiler (GCC or MinGW-w64 g++).
- Python 3.9+.
- Python packages listed in `requirements.txt` for the recorder.

Install the Python dependencies from the project root:

```bash
python -m venv .venv
```

Activate the environment on Windows PowerShell:

```powershell
.\.venv\Scripts\Activate.ps1
pip install -r requirements.txt
```

On macOS/Linux, activate it with:

```bash
source .venv/bin/activate
pip install -r requirements.txt
```

## 1. Capture Wikimedia events

Create the `data/` directory if it does not already exist. Run the recorder from the `capture/` directory because its output paths are relative to the current working directory:

```powershell
cd capture
python recorder.py
```

The recorder connects to:

```text
https://stream.wikimedia.org/v2/stream/recentchange
```

It writes accepted `edit` and `new` events to `../data/events.jsonl` and `../data/events.tsv`. It reconnects after connection failures with exponential backoff. Press `Ctrl+C` to stop recording.

## 2. Build and run the C++ engine

From the `engine/` directory, compile the C++ source with optimization enabled:

```powershell
g++ -O2 -DNDEBUG -std=c++17 engine.cpp -o engine.exe
```

Run with the defaults (TSV path `../data/events.tsv`, 600-second window, Top 10):

```powershell
.\engine.exe
```

Or pass the input file, window size in seconds, and K explicitly:

```powershell
.\engine.exe ..\data\events.tsv 600 10
```

The C++ implementation uses:

- `unordered_map` to maintain per-page edit counts.
- A min-priority queue to expire the earliest timestamp first, including when records are not perfectly timestamp-ordered.
- `partial_sort` to select the Top-K pages without fully sorting every active page.
- A `get_rank()` query that computes the 1-based rank of a page using the same tie-breaking rule.

For a window ending at time `T`, an event is active when:

```text
T - window_seconds < event_timestamp <= T
```

The program advances the event-time watermark using the maximum timestamp seen so far. The displayed processing timer starts after the input file is opened and includes reading/parsing the TSV, event ingestion, expiry, and final Top-K selection.

## 3. Run the Python baseline

From the project root:

```powershell
python naive/naive.py
```

Optional arguments are the TSV path, window size, and K:

```powershell
python naive/naive.py data/events.tsv 600 10
```

The Python program intentionally uses a simple approach: it reads and stores valid events, finds the latest timestamp, scans all events to recount those inside the final window, and sorts all active pages. Its total timer includes file opening/reading, parsing, recounting, and sorting.

## Benchmarking

Use the **same TSV file, window size, and K** for both implementations. Build C++ with optimizations enabled (`-O2 -DNDEBUG`). Run each implementation several times (ideally 5–10) and compare the median, not a single run.

The programs report slightly different timing breakdowns:

- C++: file read/parse + incremental ingestion/expiry + Top-K (the printed label currently says `Processing time (ingestion + expiry + Top-K)`, although the timer begins before the read loop).
- Python: file read/parse + full recount + full sort.

For an end-to-end comparison, compare the total times. For an algorithm-only comparison, instrument both programs to start timing after parsing has completed; do not compare one program's core time with the other's end-to-end time.

### Current sample run

On the captured dataset used during development:

| Measurement | Result |
|---|---:|
| Valid TSV rows | 158,453 |
| Skipped rows | 0 |
| Python active unique pages | 11,109 |
| Sliding window | 600 seconds |
| Top-K | 10 |

Two observed runs per implementation produced these times:

| Implementation | Run 1 | Run 2 | Two-run average |
|---|---:|---:|---:|
| C++ | 0.343821 s | 0.318364 s | 0.331093 s |
| Python naive | 0.255977 s | 0.200662 s | 0.228320 s |

Both implementations produced the same Top 10 for this dataset. In these two measurements, Python's total runtime was lower. This is an early sample, not a definitive performance claim: only two runs were recorded, and the C++ and Python timing boundaries are not perfectly identical. Re-run multiple times on the same machine before drawing conclusions. The Python breakdown also shows that file reading and timestamp parsing dominate its measured total for this dataset.

## Notes and limitations

- The current window is based on event timestamps, not wall-clock time.
- The current C++ and Python programs are offline replay/benchmark tools; they do not expose an HTTP API or continuously update a client dashboard.
- The Python implementation is intended as a simple reference baseline, not as a production streaming implementation.
- The captured dataset is local test data. Collect your own data with the recorder rather than committing large event logs.
- The flowchart image is a high-level project diagram; update it if the implementation architecture changes.

## License

This project is licensed under the MIT License.
See the [LICENSE](LICENSE) file for details.