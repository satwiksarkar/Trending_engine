import sseclient
import urllib3
import json
import os
import time

# Global delay for backoff
delay = 1

def to_record(change: dict):
    try:
        event_id = change['meta']['id']
        # Use the top-level integer timestamp (Unix epoch) instead of the ISO string
        timestamp = change['timestamp']
        wiki = change['wiki']
        # Strip tabs and newlines to keep the TSV format clean
        title = change['title'].replace('\t', ' ').replace('\n', ' ')
        
        page_key = f"{wiki}|{title}"
        return event_id, timestamp, page_key
    except KeyError:
        return None

def run_once(f_json, f_tsv):
    global delay
    url = 'https://stream.wikimedia.org/v2/stream/recentchange'
    http = urllib3.PoolManager()
    response = http.request(
        'GET',
        url,
        preload_content=False,
        headers={
            'Accept': 'text/event-stream',
            'User-Agent': 'GraviaTrendingEngine/1.0 ' 
        },
        timeout=urllib3.Timeout(connect=10.0, read=90.0),
    )
    
    if response.status != 200:
        response.release_conn()
        raise RuntimeError(f'Wikimedia stream returned HTTP {response.status}')
        
    client = sseclient.SSEClient(response)
    
    count = 0
    for event in client.events():
        # Reset backoff delay as soon as we successfully receive data
        delay = 1 
        
        if event.event != 'message':
            continue
            
        try:
            change = json.loads(event.data)
        except ValueError:
            continue 
            
        if change.get('meta', {}).get('domain') == 'canary':
            continue 
        if change.get('type') not in ('edit', 'new'):
            continue
            
        record = to_record(change)
        if record:
            event_id, ts, page_key = record
            
            f_json.write(event.data + '\n')
            f_tsv.write(f"{ts}\t{page_key}\n")
            
            count += 1
            if count % 100 == 0:
                f_json.flush()
                f_tsv.flush()

if __name__ == "__main__":
    os.makedirs('../data', exist_ok=True)
    
    print("Starting to listen to Wikimedia recent changes stream...")
    
    with open('../data/events.jsonl', 'a', encoding='utf-8') as f_json, \
         open('../data/events.tsv', 'a', encoding='utf-8') as f_tsv:
         
        while True:
            try:
                run_once(f_json, f_tsv)
            except KeyboardInterrupt:
                print("\nStopped recording gracefully.")
                break
            except Exception as e:
                print(f"Disconnected: {e!r}; retrying in {delay}s")
                f_json.flush()
                f_tsv.flush()
                time.sleep(delay)
                delay = min(delay * 2, 60)