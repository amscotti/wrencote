#!/usr/bin/env python3
"""An h2 connection must survive past the idle timeout while it stays active.

Regression: the idle timer was armed once at accept and never extended on h2
activity, so every h2 connection was force-closed five seconds in regardless
of traffic. Requests spaced closer than the idle window must keep the
connection alive well past the original deadline.
"""
import socket
import ssl
import sys
import time
from h2.connection import H2Connection
from h2.events import DataReceived, StreamEnded

PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 18106
IDLE_SECONDS = 5  # WC_HTTP_IDLE_TIMEOUT_MS default

conn = None
next_stream = [-1]


def open_stream():
    next_stream[0] += 2
    return next_stream[0]


def request_id(sock, value):
    stream = open_stream()
    conn.send_headers(
        stream,
        [(":method", "GET"), (":scheme", "https"), (":authority", "localhost"),
         (":path", f"/id?n={value}")],
        end_stream=True,
    )
    sock.sendall(conn.data_to_send())
    body = b""
    while True:
        data = sock.recv(65536)
        assert data, "connection closed before the stream ended"
        for event in conn.receive_data(data):
            if isinstance(event, DataReceived):
                body += event.data
            if isinstance(event, StreamEnded) and event.stream_id == stream:
                return body.decode()


context = ssl._create_unverified_context()
context.set_alpn_protocols(["h2"])
sock = context.wrap_socket(socket.create_connection(("127.0.0.1", PORT)),
                           server_hostname="localhost")
sock.settimeout(10.0)  # a stalled read must fail, not hang ctest
assert sock.selected_alpn_protocol() == "h2"
conn = H2Connection()
conn.initiate_connection()

assert request_id(sock, "first") == "first"

# Requests spaced well under the idle window, spanning longer than it: without
# the timer reset the connection is closed at the original 5s deadline.
start = time.monotonic()
value = 0
while time.monotonic() - start < IDLE_SECONDS + 3:
    assert request_id(sock, f"v{value}") == f"v{value}"
    value += 1
    time.sleep(1.0)

sock.close()
print(f"h2 idle: connection stayed alive across {value} spaced requests")
