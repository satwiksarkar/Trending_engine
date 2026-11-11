import sseclient
import urllib3
import json

print("Starting to listen to Wikimedia recent changes stream...")
url = 'https://stream.wikimedia.org/v2/stream/recentchange'
http = urllib3.PoolManager()
response = http.request(
    'GET',
    url,
    preload_content=False,
    headers={
        'Accept': 'text/event-stream',
        # Add a custom User-Agent here. Use your actual email.
        'User-Agent': 'GraviaTrendingEngine/1.0 (satwiksarkar3378@gmail.com)' 
    },
    timeout=urllib3.Timeout(connect=10.0, read=90.0),
)

if response.status != 200:
    response.release_conn()
    raise RuntimeError(f'Wikimedia stream returned HTTP {response.status}')

try:
    client = sseclient.SSEClient(response)
    for event in client.events():
        if not event.data:
            continue

        print(f"Received a {event.event!r} event:")
        change = json.loads(event.data)
        print(json.dumps(change, indent=2))
        break
    else:
        print("The stream closed before sending an event.")
finally:
    response.release_conn()