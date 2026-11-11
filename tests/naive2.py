import time
from collections import Counter
from pathlib import Path

def get_naive_top_10(tsv_file, window_seconds=600):
    events = []
    
    print("Ingesting Real Data TSV firehose (Naive Baseline)...")
    
    # 1. Load all events from the TSV
    with open(tsv_file, 'r', encoding='utf-8') as f:
        for line in f:
            parts = line.strip().split('\t')
            if len(parts) == 2:
                try:
                    events.append((int(parts[0]), parts[1]))
                except ValueError:
                    continue

    if not events:
        print("No events found yet.")
        return

    # Start the performance timer
    start_time = time.perf_counter()

    # 2. Get the latest timestamp
    latest_ts = max(ts for ts, page in events)
    cutoff = latest_ts - window_seconds

    # 3. Recount EVERYTHING (The O(N) Naive bottleneck)
    counts = Counter()
    for ts, page in events:
        if ts > cutoff and ts <= latest_ts:
            counts[page] += 1

    # 4. Sort and rank
    ranked = sorted(counts.items(), key=lambda x: (-x[1], x[0]))
    top_10 = ranked[:10]

    # Stop the timer
    end_time = time.perf_counter()
    elapsed = end_time - start_time

    print(f"\nNaive Top 10 at timestamp {latest_ts}:")
    for page, count in top_10:
        print(f"{count} edits\t{page}")

    print("\n-----------------------------------")
    print(f"Python Internal Execution Time: {elapsed:.6f} seconds")
    print("-----------------------------------")

if __name__ == "__main__":
    # Dynamically find the 'data/events.tsv' file relative to this script
    base_dir = Path(__file__).resolve().parent.parent
    target_file = base_dir / 'data' / 'events.tsv'
    
    if not target_file.exists():
        print(f"Error: Could not find {target_file}")
    else:
        get_naive_top_10(target_file)