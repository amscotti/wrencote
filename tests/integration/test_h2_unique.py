#!/usr/bin/env python3
import socket
import ssl
import sys
from h2.connection import H2Connection
from h2.events import DataReceived, StreamEnded

PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 18101
COUNT = 100

context = ssl._create_unverified_context()
context.set_alpn_protocols(["h2"])
sock = context.wrap_socket(socket.create_connection(("127.0.0.1", PORT)), server_hostname="localhost")
assert sock.selected_alpn_protocol() == "h2"
conn = H2Connection()
conn.initiate_connection()
streams = {}
for i in range(COUNT):
    stream_id = 1 + i * 2
    streams[stream_id] = str(i)
    conn.send_headers(
        stream_id,
        [(":method", "GET"), (":scheme", "https"), (":authority", "localhost"),
         (":path", f"/id?n={i}")],
        end_stream=True,
    )
sock.sendall(conn.data_to_send())
sock.settimeout(1.0)
bodies = {stream_id: b"" for stream_id in streams}
ended = set()
while len(ended) < COUNT:
    data = sock.recv(65536)
    if not data:
        break
    for event in conn.receive_data(data):
        if isinstance(event, DataReceived):
            bodies[event.stream_id] += event.data
        if isinstance(event, StreamEnded):
            ended.add(event.stream_id)
assert len(ended) == COUNT, (len(ended), COUNT)
for stream_id, expected in streams.items():
    assert bodies[stream_id].decode() == expected, (stream_id, bodies[stream_id], expected)
sock.close()
print(f"h2 unique: {COUNT}/{COUNT} streams matched their bodies")
